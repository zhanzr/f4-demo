# CoreMark 1.0.1 from SRAM1 @ 168 MHz — nano-f407 (STM32F407VET6)

Runs CoreMark 1.0.1 (**10,000 iterations**) with the timed benchmark kernel
(`core_*.c`) linked into **SRAM1** (0x20000000, the 112 KB main SRAM) and
copy-in'd from flash at startup; the harness/HAL/printf stay in flash. Goal:
isolate pure CPU+SRAM throughput from flash wait-state / ART influence. Timed
with the HAL SysTick 1 kHz tick (`HAL_GetTick()`, see `src/core_portme.c`).

## Why SRAM1 (not SRAM2) and a note on flash vs SRAM

Measured on hardware with byte-identical kernel code and identical SysTick
timing (default gcc flags now `-Ofast -ffp-contract=fast -funroll-all-loops`),
running the same kernel from different memories gives very different
throughput (applying the nano-f411 tuning; the SRAM2 row is from the earlier
`-funroll-loops` measurements):

| Execution memory | GCC it/s | Time (s) |
| ---------------- | --------- | -------- |
| FLASH (ART I-cache, `coremark_168m`) | 448.55 | 22.29 |
| **SRAM1 @ 0x20000000** | **330.72** | 30.24 |
| SRAM2 @ 0x2001C000 | 198.19 | 50.46 |

Two separate effects:

1. **SRAM2 is genuinely slow for instruction fetch on F407.** SRAM1
   (0x20000000) executes the kernel about **1.53× faster** than SRAM2
   (0x2001C000) on every toolchain. This project
   originally used SRAM2; it was repointed to SRAM1 because that is the faster
   home for the timed core.

2. **FLASH-with-ART is fastest of all** (~1.36× faster than SRAM1). STM32F4
   flash is fetched over the dedicated ICode bus through the ART accelerator
   (128-bit prefetch + instruction cache), so tight loops execute at near
   1 instr/cycle with no bus arbitration. SRAM has no such accelerator and is
   fetched over the shared system bus, so it is slower than flash — the
   opposite of the usual "RAM is faster" intuition. This is the irreducible
   remainder (449 vs 331 it/s) that SRAM cannot recover on this part.

The disassembly of `core_bench_list` is byte-for-byte identical between the
flash and SRAM builds, confirming the difference is purely the fetch bus /
ART, not the compiler output or a timing artifact (all results match
wall-clock time).

## Three-toolchain SRAM1 results (measured on hardware, nano-f407 — healthy chip)

SRAM1 execution works on all three toolchains. The kernel is reliably placed
at the SRAM1 base via `stm32f407coremark_sram.ld`, which places `.ram_code`
**before** `.text` (so GNU ld's first-match hands the core kernel to SRAM1 for
every compiler) and matches each core object by both `.c.obj` (gcc /
starm-clang) and `.o` (armclang) names. Stack stays at the top of SRAM1
(0x2001C000), with `.data`/`.bss`/heap laid out after the ~13 KB kernel.

| Toolchain | Flags | Iterations/Sec (SRAM1) | Time (s) |
| --------- | ----- | ---------------------- | -------- |
| GCC | `-Ofast -ffp-contract=fast -funroll-all-loops` (default) | **330.72** | 30.24 |
| ARMCLANG (Keil AC6) | `-Omax -fno-lto` | **394.38** | 25.36 |
| ST Arm clang | `-Ofast -ffp-contract=fast -funroll-loops` | 291.22 | 34.34 |

Per toolchain, only the highest measured configuration is shown. ARMCLANG's
`-Omax` wins here too (same C-only recipe as `coremark_168m` — clear
`BENCH_OPT`, set `BENCH_OPT_C="-Omax -fno-lto"`). For comparison, the same
builds in SRAM2 gave 198.19 / 224.95 / 188.75 it/s — moving to SRAM1 raised
every toolchain by ~1.5×. All three validate with `crcfinal = 0x988c`
(`Correct operation validated`) and their reported runtimes match measured
wall-clock time.

### Timing method: SysTick, not DWT/CYCCNT

Earlier builds timed the kernel with the DWT cycle counter (CYCCNT). The GCC
and ARMCLANG builds timed correctly with it, but the **ST Arm clang build
miscompiled the DWT-based timing**: `-Ofast` (and even `-O2`) produced a
self-flagged invalid result ("Must execute for at least 10 secs") with a
physically implausible rate (thousands of iterations/s across a ~54 s wall
run). Diagnostics confirmed the CPU and CYCCNT both run at a true 168 MHz and
that the timing leaf functions are compiled correctly, so this is a clang
optimizer interaction with the DWT-timing control flow in `core_main.c`, not
a board/clock/HAL fault. Switching `core_portme.c` to the HAL SysTick
1 kHz tick (a call into the HAL) yields consistent, self-validating runs on
all three toolchains (as well as the earlier — and misleadingly fast — DWT
numbers for GCC/armclang, which under-counted wall time for SRAM code), which
is why the project now times with SysTick.

> CCM (0x10000000) cannot run code on this part — see `ccm_probe` and the
> board README "RAM / CCM code-execution test status".

## Build

Requires the CMake/Ninja environment from the board-level `../../README.md`.

```bash
mkdir -p build && cd build
cmake -G Ninja ..
ninja
ninja flash          # programs via probe-rs / ST-Link (SWD)
```

Console is the board's USART1 / ST-Link VCP (115200 8-N-1); on this setup
reachable via the CH340 USB-serial port (auto-detect, e.g. COM4) — see
`../../README.md`.
