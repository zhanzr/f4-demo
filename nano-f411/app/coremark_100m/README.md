# CoreMark 1.0.1 @ 100 MHz — nano-f411 (STM32F411CEU6)

CoreMark 1.0.1 (EEMBC, `coremark_1_0_1/`), **10,000 iterations**, on the
**nano-f411** board (STM32F411CEU6) at **100 MHz** (HSE 25 MHz → PLL M=25 N=200
P=2 → SYSCLK). Compiler-agnostic: the same sources build with
**GNU arm-none-eabi-gcc**, **Keil Arm Compiler 6 (armclang)** or **ST Arm
clang** (starm-clang), selected at configure time. The CoreMark port uses the
HAL SysTick 1 kHz tick (`clock()`/`usec()`) from `src/core_portme.c`, so it
works identically on all three compilers.

## Results

Measured on hardware at 100 MHz (hard-float): capture the console while the
chip runs the benchmark (it re-runs every ~47 s), and take the last complete
`Iterations/Sec` line (earlier lines can be stale bytes from the previous
firmware still draining from the ST-Link VCP buffer).

| Toolchain | Flags | iterations/s | Time (s) | validation |
| --------- | ------| ------------ | -------- | ---------- |
| GCC | `-Ofast -ffp-contract=fast -funroll-loops` | **267.78** | 37.34 | `Correct operation validated`, crcfinal `0x988c` |
| ARMCLANG (Keil AC6) | `-Ofast -ffp-contract=fast -funroll-loops` | **288.73** | 34.63 | `Correct operation validated`, crcfinal `0x988c` |
| ST Arm clang | `-Ofast -ffp-contract=fast -funroll-loops` | **253.68** | 39.42 | `Correct operation validated`, crcfinal `0x988c` |

All runs share the same CRC (`crcfinal 0x988c`). Note CoreMark's per-run CRC
forces the work to execute, so **LTO does not inflate it** the way it cheats
Dhrystone — see `dhry_100m/LTO_on_dhrystone.md`.

## Build

Requires the CMake/Ninja environment from the board-level `../../README.md`.

```bash
# GNU gcc (default)
mkdir -p build && cd build
cmake -G Ninja ..
ninja
ninja flash          # programs the board via probe-rs / ST-Link (SWD)

# armclang (optional)
cmake -G Ninja -DSTM32_TOOLCHAIN=armclang ..
ninja

# starm-clang (optional)
cmake -G Ninja -DSTM32_TOOLCHAIN=starm-clang ..
ninja

# GNU gcc + LTO (valid here; contrast with Dhrystone — see dhry_100m/LTO_on_dhrystone.md)
cmake -G Ninja -DSTM32_TOOLCHAIN=gcc -DSTM32_LTO=ON ..
ninja
```

Use a separate build dir per toolchain (`build/`, `build-gcc-lto/`,
`build-armclang/`, `build-starm-clang/`) because `CMAKE_TOOLCHAIN_FILE` is
cached after configure.

## Measuring

CoreMark prints iterations/s and the CRC on the last line, e.g.:

```
Total time (secs) = 37.343000
Iterations/Sec   = 267.780634
Iterations/Sec   = 267.780634
```

The serial console and the capture recipe are described in the board-level
`../../README.md`. The numbers above were produced by flashing the build,
reading the console for ~45 s, and taking the last complete run (the firmware
re-runs the benchmark every ~47 s).