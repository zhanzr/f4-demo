# CoreMark 1.0.1 @ 168 MHz — nano-f407 (STM32F407VET6)

CoreMark 1.0.1 (EEMBC, `coremark_1_0_1/`), **10,000 iterations**, on the
**nano-f407** board (STM32F407VET6) at **168 MHz** (HSE 8 MHz → PLL M=8 N=336
P=2 → SYSCLK). Compiler-agnostic: the same sources build with
**GNU arm-none-eabi-gcc** or **Keil Arm Compiler 6 (armclang)**, selected at
configure time. The CoreMark port uses the HAL `clock()`/`usec()` (DWT) from
`../../board/`.

## Results (measured on hardware, 168 MHz, hard-float, GCC)

| Toolchain    | Flags                                    | iterations/s | validation |
| ------------ | ---------------------------------------- | ------------ | ---------- |
| GCC 15.3.1   | `-Ofast -ffp-contract=fast -funroll-loops` | 427.7       | OK (0x988c) |

Two consecutive runs: 427.716 iterations/s each (10,000 iterations in 23.38 s).
All runs print `Correct operation validated` and identical CRC
(crcfinal `0x988c`). (For reference, GCC `-flto` gives 426.6 iterations/s on
the dev1-f407 board — ~0.3 %, noise — because CoreMark's per-run CRC forces
the work to execute, so LTO cannot cheat it the way it cheats Dhrystone; see
`dhry_168m/LTO_on_dhrystone.md`.)

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

# GNU gcc + LTO (valid here; contrast with Dhrystone — see dhry_168m/LTO_on_dhrystone.md)
cmake -G Ninja -DSTM32_TOOLCHAIN=gcc -DSTM32_LTO=ON ..
ninja
```

Use a separate build dir per toolchain (`build/`, `build-gcc/`,
`build-gcc-lto/`) because `CMAKE_TOOLCHAIN_FILE` is cached after configure.

## Measuring

CoreMark prints iterations/s and the CRC on the last line, e.g.:

```
Total time (secs) = 23.380000
Iterations/Sec   = 427.715997
Iterations/Sec   = 427.715997
```

The serial console and the capture recipe are described in the board-level
`../../README.md`. Reading 30–60 s of console while `probe-rs reset` restarts
the benchmark, then averaging the `Iterations/Sec` lines, is what the numbers
above came from.
