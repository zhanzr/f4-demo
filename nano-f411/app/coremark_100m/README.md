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
chip runs the benchmark (it re-runs every ~37-49 s), and take the last complete
`Iterations/Sec` line (earlier lines can be stale bytes from the previous
firmware still draining from the ST-Link VCP buffer).

| Toolchain | Flags | iterations/s | Time (s) |
| --------- | ------| ------------ | -------- |
| GCC | `-Ofast -ffp-contract=fast -funroll-all-loops` + `-DSTM32_LTO=ON` | **285.34** | 35.05 |
| ARMCLANG (Keil AC6) | `-Omax -fno-lto` | **339.997** | 29.41 |
| ST Arm clang | `-Ofast -ffp-contract=fast -funroll-loops` | 253.68 | 39.42 |

Per toolchain, only the highest measured configuration is shown; the GCC
default (`-funroll-all-loops`, no LTO) is 282.35 it/s. Keil AC6 at `-Omax`
reaches **339.997 iterations/s**, matching ST's published 339 CoreMark for the
F411 @ 100 MHz from flash. Bare `-Omax` makes armclang emit the code as an
**LLVM LTO object** (`-fno-lto` in the C-only flags is what allows the GNU-ld
flow in this repo to link it; Keil MDK uses armlink, which can consume the LTO
objects directly). The same `-Omax` recipe applied to the SRAM variant is in
`coremark_sram/README.md`.

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

# armclang at -Omax (339.997 it/s; matches ST's published 339):
# BENCH_OPT_C is applied to the C files only (arm-none-eabi-gcc rejects
# -Omax at the asm/link step), and -fno-lto is required because -Omax would
# otherwise emit LTO objects GNU ld cannot link.
cmake -G Ninja -DSTM32_TOOLCHAIN=armclang '-DBENCH_OPT=' '-DBENCH_OPT_C=-Omax -fno-lto' ..
ninja
ninja flash

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