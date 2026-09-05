# CoreMark 1.0.1 @ 180 MHz - fire-f429 SDRAM app

This is the SDRAM-remapped counterpart of `bare/coremark_180m`. It reuses the
same CoreMark sources and compiler flags; `.data`, `.bss`, and heap are placed
in onboard SDRAM by `../../board/stm32f429_sdram.ld`. SDRAM is initialized by
HAL from `SystemInit()` before the C runtime.

## Results

Measured on hardware, 180 MHz, 10,000 iterations, SysTick timing:

| Build | Flags | iterations/s | validation |
| ----- | ----- | ------------ | ---------- |
| Bare, internal SRAM | `-Ofast -ffp-contract=fast -funroll-all-loops` (gcc) | **495.32** | OK (`0x988c`) |
| Bare, `-Omax` (armclang) | `-Omax -fno-lto` | **599.20** | OK (`0x988c`) |
| SDRAM app, gcc default | `-Ofast -ffp-contract=fast -funroll-all-loops` | **194.39** | OK (`0x988c`) |
| SDRAM app, `-Omax` (armclang) | `-Omax -fno-lto` | **231.59** | OK (`0x988c`) |
| SDRAM app, ST Arm clang | `-Ofast -ffp-contract=fast -funroll-loops` | 196.75 | OK (`0x988c`) |

The SDRAM figures are ~60% lower than the internal-SRAM ones, while the
matching CRC confirms the workload completed correctly. CoreMark's working
data is now in external SDRAM, so its random/list/matrix accesses pay the
SDRAM access latency. armclang `-Omax` helps here too (231.59 vs 194.39 gcc).

CoreMark's CRC validation detects incorrect or skipped work. Differences are
interpreted only when the compiler, flags, clock, and iteration count match.

## Build and flash

```bash
cmake -G Ninja -B build .
ninja -C build
ninja -C build flash
# armclang / -Omax: -DSTM32_TOOLCHAIN=armclang -DBENCH_OPT= -DBENCH_OPT_C="-Omax -fno-lto"
```