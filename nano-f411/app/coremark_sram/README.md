# CoreMark 1.0.1 from SRAM @ 100 MHz — nano-f411 (STM32F411CEU6)

Runs CoreMark 1.0.1 (**10,000 iterations**) with the timed benchmark kernel
(`core_*.c`) linked into **SRAM** (0x20000000, the single 128 KB SRAM block of
the F411) and copy-in'd from flash at startup; the harness/HAL/printf stay in
flash. Goal: isolate pure CPU+SRAM throughput from flash wait-state / ART
influence. Timed with the HAL SysTick 1 kHz tick (`HAL_GetTick()`, see
`src/core_portme.c`).

## Why SRAM, and a note on flash vs SRAM

The F411 has one contiguous 128 KB SRAM region (no SRAM1/SRAM2 split like the
F407). The benchmark core is placed at the SRAM **base** for the same reason
the F407 project used SRAM1 instead of SRAM2: it is both the largest and the
region where the copier (`startup_stm32f411xx.s`) and the linker place no
conflicting data.

The F407 measurements give the reference picture for how execution *memory*
affects CoreMark throughput on this core (byte-identical kernel code, SysTick
timing):

| Execution memory (F407 @ 168 MHz) | GCC it/s | Time (s) |
| --------------------------------- | -------- | -------- |
| FLASH (ART I-cache) | 427.75 | 23.38 |
| SRAM @ 0x20000000 | 303.83 | 32.91 |

**FLASH-with-ART is fastest of all** (~1.4× faster than SRAM): STM32F4 flash is
fetched over the dedicated ICode bus through the ART accelerator (128-bit
prefetch + instruction cache), so tight loops execute at near 1 instr/cycle
with no bus arbitration. SRAM has no such accelerator and is fetched over the
shared system bus, so it is slower than flash — the opposite of the usual "RAM
is faster" intuition. The disassembly of `core_bench_list` is byte-for-byte
identical between the flash and SRAM builds, confirming the difference is
purely the fetch bus / ART, not the compiler output or a timing artifact.

## Results

> To be measured on hardware (100 MHz, hard-float). Same run as
> `coremark_100m` but with the kernel in SRAM; the flash build of that project
> is the "FLASH (ART I-cache)" reference line to compare against.

| Toolchain | Iterations/Sec (SRAM) (to be measured) | CRC (crcfinal) |
| --------- | -------------------------------------- | -------------- |
| GCC (`-Ofast`) | — | 0x988c expected |
| ARMCLANG (Keil AC6) | — | 0x988c expected |
| ST Arm clang (`-Ofast`) | — | 0x988c expected |

(F407 reference, same three toolchains, SRAM @ 0x20000000: 303.83 / 339.33 /
291.22 it/s — all validated with `crcfinal = 0x988c`.)

### Timing method: SysTick, not DWT/CYCCNT

Earlier nano-f407 builds timed the kernel with the DWT cycle counter (CYCCNT).
The GCC and ARMCLANG builds timed correctly with it, but the **ST Arm clang
build miscompiled the DWT-based timing**: `-Ofast` (and even `-O2`) produced a
self-flagged invalid result ("Must execute for at least 10 secs") with a
physically implausible rate, while the CPU and CYCCNT were both running at the
true clock. This is a clang optimizer interaction with the DWT-timing control
flow in `core_main.c`, not a board/clock/HAL fault. Switching `core_portme.c`
to the HAL SysTick 1 kHz tick (a call into the HAL) yields consistent,
self-validating runs on all three toolchains, which is why the project times
with SysTick.

> CCM (0x10000000, 128 KB on F411) cannot run code on any F4 — it is only
> reachable over the D-bus, so instruction fetch hard-faults (see the board
> README "RAM / CCM code-execution test status"). It is therefore unused here.

## Build

Requires the CMake/Ninja environment from the board-level `../../README.md`.

```bash
mkdir -p build && cd build
cmake -G Ninja ..
ninja
ninja flash          # programs via probe-rs / ST-Link (SWD)
# armclang / starm-clang via -DSTM32_TOOLCHAIN=... as in coremark_100m
```

Console is the board's USART1 / ST-Link VCP (115200 8-N-1) — see
`../../README.md`.