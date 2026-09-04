# CoreMark 1.0.1 from SRAM1 @ 168 MHz — dev1-f407 (STM32F407VET6)

Runs CoreMark 1.0.1 (**10,000 iterations**) with the timed benchmark kernel
(`core_*.c`) linked into **SRAM1** (0x20000000, the 112 KB main SRAM) and
copy-in'd from flash at startup; the harness/HAL/printf stay in flash. Goal:
isolate pure CPU+SRAM throughput from flash wait-state / ART influence. Timed
with the HAL **SysTick** 1 kHz tick (`HAL_GetTick()`, see `src/core_portme.c`).

SRAM1 is used instead of SRAM2 (0x2001C000, 16 KB): SRAM2 cannot hold even a
`-Omax` armclang kernel (~24 KB), and SRAM1 at 0x20000000 is the faster
instruction-fetch home on F407 (nano-f407 measured ~1.53× over SRAM2).

## Timing: SysTick, not DWT/CYCCNT (and why)

This port previously timed with the ARM DWT cycle counter (CYCCNT). On SRAM
runs the DWT numbers were **wrong — inflated**, e.g. GCC 460 "it/s" that a
wall-clock check shows is really ~211 (on SRAM2). The cause is a **32-bit
counter rollover**, not precision or SRAM-specific "under-counting":

- CYCCNT is a free-running **32-bit** counter. At 168 MHz it wraps every
  **2^32 / 168 MHz ≈ 25.57 s**.
- Flash runs (~22 s) finish before the wrap → DWT agrees with SysTick
  (flash: 448.47 SysTick vs 449.00 DWT).
- SRAM runs take **30-53 s** (> 25.57 s). The wrap-inclusive difference
  `stop-start` under-reports the true elapsed time by one wrap period
  (~25.57 s), inflating `Iterations/Sec`.
- The DWT timing is therefore not valid for runs longer than ~25.6 s. SysTick
  is a 32-bit ms counter with a ~49.7-day period, so it cannot wrap on any
  CoreMark run and is used here (and on the nano-f407/nano-f411 SRAM ports).

## Results (measured on hardware, SysTick timing, kernel verified in SRAM1)

| Toolchain | Flags | Iterations/Sec (SRAM1) | Time (s) | CRC (crcfinal) |
| --------- | ----- | ---------------------- | -------- | -------------- |
| GCC | `-Ofast -ffp-contract=fast -funroll-all-loops` (default) | **330.72** | 30.24 | 0x988c |
| ARMCLANG (Keil AC6) | `-Omax -fno-lto` | **394.38** | 25.36 | 0x988c |
| ST Arm clang | `-Ofast -ffp-contract=fast -funroll-loops` | 291.22 | 34.34 | 0x988c |

Per toolchain, only the highest measured configuration is shown (the kernel
was confirmed in SRAM1 via the link map for every row). All runs print
`Correct operation validated` with `crcfinal 0x988c`.

These exactly match the nano-f407 SRAM1 numbers for the same flags
(330.72/394.38/291.22) — same F407 silicon, same 112 KB SRAM1 base, same
SysTick timer. Compare the same kernels executing from FLASH (ART I-cache,
`coremark_168m` on this board): GCC 448.47 / ARMCLANG `-Omax` 541.15 / ST
401.41 it/s. FLASH+ART stays fastest, as on the other boards.

## Build

Requires the CMake/Ninja environment from the board-level `../../README.md`.

```bash
mkdir -p build && cd build
cmake -G Ninja ..
ninja
ninja flash          # programs via probe-rs / ULINK2 (SWD)
# armclang / starm-clang via -DSTM32_TOOLCHAIN=... as in coremark_168m
# armclang -Omax: -DBENCH_OPT= -DBENCH_OPT_C="-Omax -fno-lto"
```

Console is the board's USART3 / USB-serial (115200 8-N-1), same as
`blink_hello` — see `../../README.md`.