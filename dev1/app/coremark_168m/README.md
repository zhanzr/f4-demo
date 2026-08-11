# CoreMark 1.0.1 @ 168 MHz — dev1 (STM32F407VET6)

CoreMark 1.0.1 (EEMBC, `coremark_1_0_1/`), **10,000 iterations**, on the
**dev1** board (custom STM32F407VET6) at **168 MHz** (HSE 25 MHz → PLL M=25
N=336 P=2 → SYSCLK). Compiler-agnostic: the same sources build with
**GNU arm-none-eabi-gcc** or **Keil Arm Compiler 6 (armclang)**, selected at
configure time. The CoreMark port uses the HAL `clock()`/`usec()` (DWT) from
`../../board/`.

## Results (measured on hardware, 168 MHz, hard-float)

Normal toolchain comparison (no LTO):

| Toolchain                | Flags                                    | iterations/s | validation |
| ------------------------ | ---------------------------------------- | ------------ | ---------- |
| GCC 15.3.1               | `-Ofast -ffp-contract=fast -funroll-loops` | 427.7       | OK (0x988c) |
| armclang 6.24 (Keil AC6) | `-Ofast -ffp-contract=fast -funroll-loops` | 451.0       | OK (0x988c) |

All runs print `Correct operation validated` and identical CRC
(crcfinal `0x988c`). (For reference, GCC `-flto` gives 426.6 iterations/s —
~0.3 %, noise — because CoreMark's per-run CRC forces the work to execute, so
LTO cannot cheat it the way it cheats Dhrystone; see `dhry_168m/LTO_on_dhrystone.md`.)

armclang is ~5.5 % faster than GCC here. That does **not** justify adopting
Arm Compiler 6: it is in maintenance (6.24 is the **final feature release**,
defect fixes only), and this project only links it through a GNU-ld/newlib
hybrid that needs two shims (see the board-level `../../README.md`). For new
work prefer the open **LLVM Embedded Toolchain** or GNU gcc.

## Build

Requires the CMake/Ninja environment from the board-level `../../README.md`.

```bash
# armclang (default)
mkdir -p build && cd build
cmake -G Ninja ..
ninja
ninja flash          # programs the board via probe-rs / ULINK2 (SWD)

# GNU gcc
cmake -G Ninja -DSTM32_TOOLCHAIN=gcc ..
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
Total time (secs) = 22.1463
Iterations/Sec   = 451.007530
Iterations/Sec   = 451.007530
```

The serial console and the capture recipe are in the board-level
`../../README.md` (USART3, PD8 TX / PD9 RX, 115200 8-N-1). Reading 30–45 s of
console while `probe-rs reset` restarts the benchmark, then averaging the
`Iterations/Sec` lines, is what the numbers above came from.
