# Dhrystone 2.1 @ 180 MHz - fire-f429 SDRAM app

This is the SDRAM-remapped counterpart of `bare/dhry_180m`. It reuses the
same Dhrystone sources and compiler flags; `.data`, `.bss`, and heap are placed
in onboard SDRAM by `../../board/stm32f429_sdram.ld`. SDRAM is initialized by
HAL from `SystemInit()` before the C runtime.

## Results

Measured on hardware with GCC 15.3.1, `-Ofast -ffp-contract=fast -funroll-loops`,
180 MHz, no LTO:

| Build | Dhrystones/s | DMIPS/MHz |
| ----- | ------------ | --------- |
| Bare, internal SRAM | 391,198 | 1.237 |
| SDRAM app | 175,700.609 | 0.556 |

The SDRAM result is about 55.1% lower than the internal-SRAM result. The
benchmark still passes all final-value checks; the reduction is the expected
cost of placing its frequently accessed globals and heap in external SDRAM.

The comparison is meaningful only with matching compiler, flags, clock, and
benchmark run count. Dhrystone must remain LTO-free because LTO can hoist work
out of its timed loop; see the bare project's LTO note.

## Build and flash

```bash
cmake -G Ninja -B build .
ninja -C build
ninja -C build flash
```
