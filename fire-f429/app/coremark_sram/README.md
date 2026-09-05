# CoreMark 1.0.1, kernel in SRAM1 + data in SDRAM @ 180 MHz — fire-f429

This is the SDRAM-remapped counterpart of `bare/coremark_sram`: the timed
kernel runs from **SRAM1** (0x20000000) while `.data`/`.bss`/heap — and
CoreMark's working data — sit in the onboard **SDRAM** (same convention as
`app/coremark_180m`). SDRAM is initialized by HAL from `SystemInit()` before
the C runtime; the inferred kernel is copy-in'd to SRAM1 at startup.

## Results (measured on hardware, SysTick timing, kernel verified in SRAM1)

| Toolchain | Flags | Iterations/Sec | Time (s) | CRC (crcfinal) |
| --------- | ----- | -------------- | -------- | -------------- |
| GCC | `-Ofast -ffp-contract=fast -funroll-all-loops` (default) | **168.57** | 59.32 | 0x988c |
| ARMCLANG (Keil AC6) | `-Omax -fno-lto` | **200.45** | 49.89 | 0x988c |
| ST Arm clang | `-Ofast -ffp-contract=fast -funroll-loops` | 168.10 | 59.49 | 0x988c |

Per toolchain, only the highest measured configuration is shown. All runs
print `Correct operation validated` with `crcfinal 0x988c`.

For comparison, the same kernels with data in internal SRAM
(`bare/coremark_sram`): GCC 382.57 / ARMCLANG 450.05 it/s, and the
all-flash/SDRAM-data app (`app/coremark_180m`): GCC 194.39 / ARMCLANG 231.59
it/s. Moving the kernel from flash to SRAM1 *reduces* the app figure
(194.39→168.57 gcc) because the SRAM1 kernel now shares the internal-bus port
with the SDRAM-backed working data; with internal-SRAM data the kernel-in-SRAM
ranking is unchanged. Treat the app figures as the mixed-memory case.

## Build and flash

```bash
cmake -G Ninja -B build .
ninja -C build
ninja -C build flash
# armclang / -Omax as in app/coremark_180m
```