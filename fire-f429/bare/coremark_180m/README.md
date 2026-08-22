# CoreMark 1.0.1 @ 180 MHz — fire-f429 (STM32F429IGT6)

CoreMark 1.0.1 (EEMBC, `coremark_1_0_1/`), **10,000 iterations**, on the
**fire-f429** board (STM32F429IGT6, bare metal) at **180 MHz** (HSE 25 MHz →
PLL M=25 N=360 P=2 → SYSCLK). Compiler-agnostic: the same sources build with
**GNU arm-none-eabi-gcc** or **Keil Arm Compiler 6 (armclang)**, selected at
configure time. The CoreMark port uses the HAL `clock()`/`usec()` (DWT) from
`../../board/`.

## Results (measured on hardware, 180 MHz, hard-float, GCC)

| Toolchain    | Flags                                      | iterations/s | validation |
| ------------ | ------------------------------------------ | ------------ | ---------- |
| GCC 15.3.1   | `-Ofast -ffp-contract=fast -funroll-loops` | 470.2        | OK (0x988c) |

Two consecutive runs: 470.234 iterations/s each (10,000 iterations in
21.27 s). All runs print `Correct operation validated` and identical CRC
(crcfinal `0x988c`).

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

# GNU gcc + LTO (valid here; contrast with Dhrystone — see dhry_180m/LTO_on_dhrystone.md)
cmake -G Ninja -DSTM32_TOOLCHAIN=gcc -DSTM32_LTO=ON ..
ninja
```

Use a separate build dir per toolchain (`build/`, `build-gcc/`,
`build-gcc-lto/`) because `CMAKE_TOOLCHAIN_FILE` is cached after configure.

## Measuring

CoreMark prints iterations/s and the CRC on the last line, e.g.:

```
Total time (secs) = 21.266000
Iterations/Sec   = 470.234177
Iterations/Sec   = 470.234177
```

The serial console and the capture recipe are described in the board-level
`../../README.md`. Reading 30–60 s of console while resetting the board, then
averaging the `Iterations/Sec` lines, is what the numbers above came from.