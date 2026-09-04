# Dhrystone 2.1 @ 100 MHz — nano-f411 (STM32F411CEU6)

Classic Dhrystone 2.1 (dhry_1.c / dhry_2.c / dhry.h), **2,000,000 runs**, on
the **nano-f411** board (STM32F411CEU6) clocked at **100 MHz** (HSE 25 MHz,
PLL M=25 N=200 P=2 → SYSCLK). Compiler-agnostic: the same sources build with
either **GNU arm-none-eabi-gcc** or **Keil Arm Compiler 6 (armclang)**,
selected at configure time.

## Results

Measured on hardware at 100 MHz (hard-float): capture the console while the
chip runs the benchmark (it re-runs every ~19 s), and take the last complete
`Dhrystones per Second` line.

| Toolchain    | Flags                                    | Dhrystones/s | DMIPS/MHz |
| ------------ | ---------------------------------------- | ------------ | --------- |
| GCC | `-Ofast -ffp-contract=fast -funroll-loops` | **235,211** | **1.339** |
| ARMCLANG (Keil AC6) | `-Ofast -ffp-contract=fast -funroll-loops` | **251,193** | **1.430** |

All runs print correct final values (Int_Glob=5, Arr_2_Glob = runs+10, …).

> ⚠ **Do not use LTO for Dhrystone.** GCC `-flto` sees the whole program and
> hoists the loop-invariant work out of the timed loop, inflating the score
> ~2.1× (to 494,927 Dhrystones/s on this board) while still passing the
> final-value check. The LTO number is meaningless and is **excluded from the
> comparison above**. Full explanation and reproduction:
> **`LTO_on_dhrystone.md`** in this folder.

The toolchain choice is discussed in the board-level `../../README.md`: **Arm
Compiler for Embedded 6.24 is the final feature release** (defect fixes only),
so GCC is recommended for production work.

## Most aggressive flags

- **ARMCLANG (Keil AC6): `-Omax` gives nothing over `-Ofast`** for Dhrystone —
  both measure 251,193 Dhrystones/s. The timed loop is already fully
  optimized, so use the default `-Ofast -ffp-contract=fast -funroll-loops`.
- **GCC:** `-Ofast -ffp-contract=fast -funroll-loops` (default) →
  235,211 Dhrystones/s; **do not** add `-flto` (inflates to 494,927 — an
  artifact, see `LTO_on_dhrystone.md`).
- **ST Arm clang:** not supported for Dhrystone on this board (gcc/armclang
  only).

```bash
cmake -G Ninja -DSTM32_TOOLCHAIN=armclang ..
cmake -G Ninja -DSTM32_TOOLCHAIN=gcc ..
```

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