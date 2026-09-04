# CoreMark 1.0.1 @ 168 MHz — dev1-f407 (STM32F407VET6)

CoreMark 1.0.1 (EEMBC, `coremark_1_0_1/`), **10,000 iterations**, on the
**dev1-f407** board (custom STM32F407VET6) at **168 MHz** (HSE 25 MHz → PLL
M=25 N=336 P=2 → SYSCLK). Compiler-agnostic: the same sources build with
**GNU arm-none-eabi-gcc**, **Keil Arm Compiler 6 (armclang)** or **ST Arm
clang** (starm-clang), selected at configure time. The CoreMark port uses the
HAL `clock()`/`usec()` (DWT) from `../../board/`.

## Results

Measured on hardware at 168 MHz (hard-float): capture the console while the
chip runs the benchmark, and take the last complete `Iterations/Sec` line
(earlier lines can be stale bytes from the previous firmware still draining
from the console adapter's buffer).

| Toolchain | Flags | iterations/s | Time (s) |
| --------- | ------| ------------ | -------- |
| GCC | `-Ofast -ffp-contract=fast -funroll-all-loops` (default) | **449.00** | 22.27 |
| GCC + LTO | default `+ -DSTM32_LTO=ON` | 447.57 | 22.34 |
| ARMCLANG (Keil AC6) | `-Omax -fno-lto` | **542.09** | 18.45 |
| ST Arm clang | `-Ofast -ffp-contract=fast -funroll-loops` | 401.43 | 24.91 |

Per toolchain, only the highest measured configuration is shown. All runs
print `Correct operation validated` with an identical CRC (crcfinal `0x988c`).

The `-funroll-all-loops` default is the nano-f411 tuning (`-Ofast`-class);
**ARMCLANG at `-Omax` is the fastest** at 542.09 iterations/s. Bare `-Omax`
makes armclang emit LLVM LTO objects that GNU ld cannot link — see the
per-toolchain recipes below. GCC `-flto` adds nothing (447.57 vs 449.00,
noise) because CoreMark's per-run CRC forces the work to execute; contrast
with Dhrystone (`dhry_168m/LTO_on_dhrystone.md`).

## Most aggressive flags

Highest measured score per toolchain (see Results):

- **ARMCLANG (Keil AC6): `-Omax -fno-lto`** — 542.09 it/s. Pass it as a
  **C-only** flag (`BENCH_OPT_C`) so it stays off the asm/link steps, with
  `BENCH_OPT` cleared (`-fno-lto` stops the LLVM LTO objects GNU ld cannot
  use; Keil MDK links them directly with armlink).
- **GCC:** `-Ofast -ffp-contract=fast -funroll-all-loops` (default) →
  449.00 it/s. `-DSTM32_LTO=ON` is unchanged (447.57).
- **ST Arm clang:** `-Ofast -ffp-contract=fast -funroll-loops` (default) →
  401.43 it/s.

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
ninja flash          # programs the board via probe-rs / ULINK2 (SWD)

# armclang (optional)
cmake -G Ninja -DSTM32_TOOLCHAIN=armclang ..
ninja
```

Use a separate build dir per toolchain (`build/`, `build-armclang/`)
because `CMAKE_TOOLCHAIN_FILE` is cached after configure.

## Measuring

CoreMark prints iterations/s and the CRC on the last line, e.g.:

```
Total time (secs) = 22.271708
Iterations/Sec   = 449.000134
Iterations/Sec   = 449.000134
```

The serial console and the capture recipe are described in the board-level
`../../README.md`. Reading 30–45 s of console while `probe-rs reset` restarts
the benchmark, then averaging the `Iterations/Sec` lines, is what the numbers
above came from.
