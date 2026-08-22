# Dhrystone 2.1 @ 168 MHz — nano-f407 (STM32F407VET6)

Classic Dhrystone 2.1 (dhry_1.c / dhry_2.c / dhry.h), **2,000,000 runs**, on
the **nano-f407** board (STM32F407VET6) clocked at **168 MHz** (HSE 8 MHz,
PLL M=8 N=336 P=2 → SYSCLK). Compiler-agnostic: the same sources build with
either **GNU arm-none-eabi-gcc** or **Keil Arm Compiler 6 (armclang)**,
selected at configure time.

## Results (measured on hardware, 168 MHz, hard-float, GCC)

| Toolchain    | Flags                                    | Dhrystones/s | DMIPS/MHz |
| ------------ | ---------------------------------------- | ------------ | --------- |
| GCC 15.3.1   | `-Ofast -ffp-contract=fast -funroll-loops` | 355,082     | 1.203     |

Two consecutive runs: 355,114 / 355,051 Dhrystones/s (2.816 / 2.817 µs per run).
All runs print correct final values (Int_Glob=5, Arr_2_Glob = runs+10, …).

> ⚠ **Do not use LTO for Dhrystone.** GCC `-flto` sees the whole program and
> hoists the loop-invariant work out of the timed loop, inflating the score
> ~2.2× (to 770,713 Dhrystones/s) while still passing the final-value check.
> The LTO number is meaningless and is **excluded from the comparison above**.
> Full explanation and reproduction: **`LTO_on_dhrystone.md`** in this folder.

The toolchain choice is discussed in the board-level `../../README.md`: **Arm
Compiler for Embedded 6.24 is the final feature release** (defect fixes only),
so GCC is recommended for production work.

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

# GNU gcc + LTO (kept only as reproducible evidence of the artifact — see LTO_on_dhrystone.md)
cmake -G Ninja -DSTM32_TOOLCHAIN=gcc -DSTM32_LTO=ON ..
ninja
```

Use a separate build dir per toolchain (`build/`, `build-gcc/`,
`build-gcc-lto/`) because `CMAKE_TOOLCHAIN_FILE` is cached after configure.

## Why the armclang printf shim?

Keil's armclang runs in ARMCLIB "standardlib" mode and specializes calls to
the *name* `printf` into the ARMCLIB ABI (`__2printf` + hidden `_printf_*`
helpers). Those symbols only exist in ARMCLIB, so linking against GNU
newlib with GNU ld fails. `../../cmake/printf_rename.h` renames `printf` →
`bench_printf` (a `vprintf` wrapper in `../../board/uart_printf.c`); armclang
does not specialize `vprintf`, so all console output still reaches the UART.

## Console

Results are printed over the board's console UART (see the board-level
`../../README.md` for the port/baud and a capture recipe).
