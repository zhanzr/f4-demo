# CoreMark 1.0.1 from SRAM2 @ 168 MHz — dev1-f407 (STM32F407VET6)

Runs CoreMark 1.0.1 (**10,000 iterations**) with the timed benchmark kernel
(`core_*.c`) linked into **SRAM2** (0x2001C000, 16 KB) and copy-in'd from
flash at startup; the harness/HAL/printf stay in flash. Goal: isolate pure
CPU+SRAM throughput from flash wait-state / ART influence. Timed with the HAL
**SysTick** 1 kHz tick (`HAL_GetTick()`, see `src/core_portme.c`).

## Timing: SysTick, not DWT/CYCCNT (and why)

This port previously timed with the ARM DWT cycle counter (CYCCNT). On SRAM
runs the DWT numbers were **wrong — inflated**, e.g. GCC 460 "it/s" that a
wall-clock check shows is really ~211. The cause is a **32-bit counter
rollover**, not precision or SRAM-specific "under-counting":

- CYCCNT is a free-running **32-bit** counter. At 168 MHz it wraps every
  **2^32 / 168 MHz ≈ 25.57 s**.
- Flash runs (~22 s) finish before the wrap → DWT agrees with SysTick
  (flash: 448.47 SysTick vs 449.00 DWT).
- SRAM2 runs take **~45-50 s** (> 25.57 s). The wrap-inclusive difference
  `stop-start` under-reports the true elapsed time by one wrap period
  (~25.57 s): e.g. real 47.30 s reports as 47.30 − 25.57 = 21.73 s, i.e.
  `Iterations/Sec` jumps 211 → 460. This is exactly the observed DWT number.
- The DWT timing is therefore not valid for runs longer than ~25.6 s. SysTick
  is a 32-bit ms counter with a ~49.7-day period, so it cannot wrap on any
  CoreMark run and is used here (and on the nano-f407/nano-f411 SRAM ports).

(Regardless of the timer-wrap issue, there is no reason to expect SRAM2 to be
*faster* than flash here: STM32F4 flash is fetched through the ART accelerator
with a dedicated I-CODE bus, while SRAM2 sits on the shared system bus. The
official ST numbers that show a tiny SRAM advantage are for SRAM1 with the
different F4xx part-week configuration, not for this SRAM2 placement.)

## Results (measured on hardware, SysTick timing, kernel verified in SRAM2)

| Toolchain | Flags | Iterations/Sec (SRAM2) | Time (s) | CRC (crcfinal) |
| --------- | ----- | ---------------------- | -------- | -------------- |
| GCC | `-Ofast -ffp-contract=fast -funroll-all-loops` (default) | **211.43** | 47.30 | 0x988c |
| ARMCLANG (Keil AC6) | `-Ofast -ffp-contract=fast -funroll-loops` | **224.95** | 44.46 | 0x988c |
| ST Arm clang | `-Ofast -ffp-contract=fast -funroll-loops` | 188.75 | 52.98 | 0x988c |

Per toolchain, only the highest measured configuration is shown (the kernel
was confirmed in SRAM2 via the link map for every row). All runs print
`Correct operation validated` with `crcfinal 0x988c`.

Compare the same kernels executing from FLASH (ART I-cache, `coremark_168m`
on this board): GCC 448.47 / ARMCLANG `-Omax` 541.15 / ST 401.41 it/s. Even
with the timer fixed, SRAM2 is ~35 % behind flash here — the ART-cached flash
path wins, as on the other boards.

> armclang `-Omax` does **not fit** in the 16 KB SRAM2 (the `-Omax` kernel is
> ~24 KB), so the armclang row uses `-Ofast`. Before this was understood, the
> `-Omax` "SRAM2" build silently kept its kernel in flash (the linker script
> matched only `*.c.obj`; that also made the old 567 "SRAM2" figure a flash
> run). The linker script now matches both `*.c.obj` (gcc) and `*.o`
> (armclang) and places `.ram_code` before `.text` so LLD (starm-clang) picks
> it up too.

## Build

Requires the CMake/Ninja environment from the board-level `../../README.md`.

```bash
mkdir -p build && cd build
cmake -G Ninja ..
ninja
ninja flash          # programs via probe-rs / ULINK2 (SWD)
# armclang / starm-clang via -DSTM32_TOOLCHAIN=... as in coremark_168m
# (armclang fits SRAM2 only at -Ofast, not -Omax)
```

Console is the board's USART3 / USB-serial (115200 8-N-1), same as
`blink_hello` — see `../../README.md`.