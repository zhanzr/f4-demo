# CoreMark 1.0.1 @ 100 MHz — nano-f411 (STM32F411CEU6)

CoreMark 1.0.1 (EEMBC, `coremark_1_0_1/`), **10,000 iterations**, on the
**nano-f411** board (STM32F411CEU6) at **100 MHz** (HSE 25 MHz → PLL M=25 N=200
P=2 → SYSCLK). Compiler-agnostic: the same sources build with
**GNU arm-none-eabi-gcc**, **Keil Arm Compiler 6 (armclang)** or **ST Arm
clang** (starm-clang), selected at configure time. The CoreMark port uses the
HAL SysTick 1 kHz tick (`clock()`/`usec()`) from `src/core_portme.c`, so it
works identically on all three compilers.

## Results

> To be measured on hardware (100 MHz, hard-float). The table below mirrors the
> nano-f407 board's 168 MHz runs for reference; fill in the F411 numbers by
> capturing the console while `probe-rs reset` restarts the benchmark
> (see "Measuring" below).

| Toolchain | Flags | iterations/s (to be measured) | validation |
| --------- | ------| ----------------------------- | ---------- |
| GCC | `-Ofast -ffp-contract=fast -funroll-loops` | — | — |
| ARMCLANG (Keil AC6) | `-Omax` | — | — |
| ST Arm clang | `-Ofast` | — | — |

All runs are expected to print `Correct operation validated` with the same CRC
(crcfinal `0x988c`). (Reference — F407 @ 168 MHz: GCC 427.7 it/s in 23.38 s;
GCC `-flto` gave 426.6 it/s, ~0.3 % noise, because CoreMark's per-run CRC
forces the work to execute, so LTO cannot cheat it the way it cheats
Dhrystone — see `dhry_100m/LTO_on_dhrystone.md`.)

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
Total time (secs) = 23.380000
Iterations/Sec   = 427.715997
Iterations/Sec   = 427.715997
```

The serial console and the capture recipe are described in the board-level
`../../README.md`. Reading 30–60 s of console while `probe-rs reset` restarts
the benchmark, then averaging the `Iterations/Sec` lines, is how the (future)
numbers above should be produced.