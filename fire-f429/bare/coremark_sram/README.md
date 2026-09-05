# CoreMark 1.0.1 from SRAM1 @ 180 MHz — fire-f429 (STM32F429IGT6)

Runs CoreMark 1.0.1 (**10,000 iterations**) with the timed benchmark kernel
(`core_*.c`) linked into **SRAM1** (0x20000000, the 112 KB first SRAM block)
and copy-in'd from flash at startup; the harness/HAL/printf stay in flash.
Goal: isolate pure CPU+SRAM throughput from flash wait-state / ART influence.
Timed with the HAL **SysTick** 1 kHz tick (`HAL_GetTick()`, see
`src/core_portme.c`).

## Why SRAM1 (not SRAM3)

The STM32F429 has SRAM1 (112 KB @ 0x20000000), SRAM2 (16 KB @ 0x2001C000) and
SRAM3 (64 KB @ 0x20020000). Both extremes were measured on hardware (SysTick
timing, identical `-Ofast -funroll-all-loops` kernels):

| Kernel region | GCC it/s | Time (s) |
| ------------- | -------- | -------- |
| **SRAM1 @ 0x20000000** | **382.57** | 26.14 |
| SRAM3 @ 0x20020000 | 252.86 | 39.55 |

SRAM1 wins by **~1.51×**, so it is the only variant kept (SRAM3 dropped) —
the same ranking as the F407 boards (SRAM1 fastest, the outer SRAMs slower for
instruction fetch).

## Results (measured on hardware, SysTick timing, kernel verified in SRAM1)

| Toolchain | Flags | Iterations/Sec (SRAM1) | Time (s) | CRC (crcfinal) |
| --------- | ----- | ---------------------- | -------- | -------------- |
| GCC | `-Ofast -ffp-contract=fast -funroll-all-loops` (default) | **382.57** | 26.14 | 0x988c |
| ARMCLANG (Keil AC6) | `-Omax -fno-lto` | **450.05** | 22.22 | 0x988c |
| ST Arm clang | `-Ofast -ffp-contract=fast -funroll-loops` | 339.03 | 29.50 | 0x988c |

Per toolchain, only the highest measured configuration is shown (the kernel
was confirmed in SRAM1 via the link map for every row). All runs print
`Correct operation validated` with `crcfinal 0x988c`.

Compare the same kernels executing from FLASH (ART I-cache, `coremark_180m`
on this board): GCC 495.32 / ARMCLANG `-Omax` 599.20 it/s. FLASH+ART stays
fastest, as on the other boards.

## Build

Requires the CMake/Ninja environment from the board-level `../../README.md`.

```bash
mkdir -p build && cd build
cmake -G Ninja ..
ninja
ninja flash          # programs via OpenOCD (ULINK2 SWD)
# armclang via -DSTM32_TOOLCHAIN=armclang, -Omax via
# -DBENCH_OPT= -DBENCH_OPT_C="-Omax -fno-lto" as in coremark_180m
```

Console is the board's USART1 / USB-serial VCP (115200 8-N-1) — see
`../../README.md`.