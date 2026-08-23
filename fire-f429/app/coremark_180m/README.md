# CoreMark 1.0.1 @ 180 MHz - fire-f429 SDRAM app

This is the SDRAM-remapped counterpart of `bare/coremark_180m`. It reuses the
same CoreMark sources and compiler flags; `.data`, `.bss`, and heap are placed
in onboard SDRAM by `../../board/stm32f429_sdram.ld`. SDRAM is initialized by
HAL from `SystemInit()` before the C runtime.

## Results

Measured on hardware with GCC 15.3.1, `-Ofast -ffp-contract=fast -funroll-loops`,
180 MHz, 10,000 iterations:

| Build | iterations/s | validation |
| ----- | ------------ | ---------- |
| Bare, internal SRAM | 470.234 | OK (`0x988c`) |
| SDRAM app | 194.246 | OK (`0x988c`) |

The SDRAM result is about 58.7% lower than the internal-SRAM result, while the
matching CRC confirms that the workload completed correctly. CoreMark's
working data is now in external SDRAM, so its random/list/matrix accesses pay
the SDRAM access latency.

CoreMark's CRC validation detects incorrect or skipped work. Differences are
interpreted only when the compiler, flags, clock, and iteration count match.

## Build and flash

```bash
cmake -G Ninja -B build .
ninja -C build
ninja -C build flash
```
