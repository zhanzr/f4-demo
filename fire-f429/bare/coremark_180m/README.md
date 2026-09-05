# CoreMark 1.0.1 @ 180 MHz — fire-f429 (STM32F429IGT6)

CoreMark 1.0.1 (EEMBC, `coremark_1_0_1/`), **10,000 iterations**, on the
**fire-f429** board (STM32F429IGT6, bare metal) at **180 MHz** (HSE 25 MHz →
PLL M=25 N=360 P=2 → SYSCLK). Compiler-agnostic: the same sources build with
**GNU arm-none-eabi-gcc**, **Keil Arm Compiler 6 (armclang)** or **ST Arm
clang** (starm-clang), selected at configure time. The CoreMark port uses the
HAL's SysTick 1 kHz tick (`HAL_GetTick()`).

## Results

Measured on hardware at 180 MHz (hard-float): capture the console while the
chip runs the benchmark, and take the last complete `Iterations/Sec` line
(earlier lines can be stale bytes from the previous firmware still draining
from the console adapter's buffer).

| Toolchain | Flags | iterations/s | Time (s) |
| --------- | ------| ------------ | -------- |
| GCC | `-Ofast -ffp-contract=fast -funroll-all-loops` (default) | **495.32** | 20.19 |
| GCC + LTO | default `+ -DSTM32_LTO=ON` | 490.70 | 20.38 |
| ARMCLANG (Keil AC6) | `-Omax -fno-lto` | **599.20** | 16.69 |
| ST Arm clang | `-Ofast -ffp-contract=fast -funroll-loops` | 448.01 | 22.32 |

Per toolchain, only the highest measured configuration is shown. All runs
print `Correct operation validated` with an identical CRC (crcfinal `0x988c`).

The `-funroll-all-loops` default is the nano-f411 tuning (`-Ofast`-class);
**ARMCLANG at `-Omax` is the fastest** at 599.20 iterations/s. Bare `-Omax`
makes armclang emit LLVM LTO objects that GNU ld cannot link — see the
per-toolchain recipes below. GCC `-flto` adds nothing (490.70 vs 495.32,
noise) because CoreMark's per-run CRC forces the work to execute; contrast
with Dhrystone (`dhry_180m/LTO_on_dhrystone.md`).

## Most aggressive flags

Highest measured score per toolchain (see Results):

- **ARMCLANG (Keil AC6): `-Omax -fno-lto`** — 599.20 it/s. Pass it as a
  **C-only** flag (`BENCH_OPT_C`) so it stays off the asm/link steps, with
  `BENCH_OPT` cleared (`-fno-lto` stops the LLVM LTO objects GNU ld cannot
  use; Keil MDK links them directly with armlink).
- **GCC:** `-Ofast -ffp-contract=fast -funroll-all-loops` (default) →
  495.32 it/s. `-DSTM32_LTO=ON` is unchanged (490.70).
- **ST Arm clang:** `-Ofast -ffp-contract=fast -funroll-loops` (default) →
  448.01 it/s.

```bash
cmake -G Ninja -DSTM32_TOOLCHAIN=armclang '-DBENCH_OPT=' '-DBENCH_OPT_C=-Omax -fno-lto' ..
cmake -G Ninja -DSTM32_TOOLCHAIN=gcc ..
cmake -G Ninja -DSTM32_TOOLCHAIN=starm-clang ..
```

## Build

Requires the CMake/Ninja environment from the board-level `../../README.md`.

```bash
# GNU gcc (default)
mkdir -p build && cd build
cmake -G Ninja ..
ninja
ninja flash          # programs the board via OpenOCD (ULINK2 SWD)

# armclang (optional)
cmake -G Ninja -DSTM32_TOOLCHAIN=armclang ..
ninja
```

Use a separate build dir per toolchain (`build/`, `build-armclang/`)
because `CMAKE_TOOLCHAIN_FILE` is cached after configure.

## Measuring

CoreMark prints iterations/s and the CRC on the last line, e.g.:

```
Total time (secs) = 20.189000
Iterations/Sec   = 495.319233
Iterations/Sec   = 495.319233
```

The serial console and the capture recipe are described in the board-level
`../../README.md`. Reading 30–60 s of console while resetting the board, then
averaging the `Iterations/Sec` lines, is what the numbers above came from.