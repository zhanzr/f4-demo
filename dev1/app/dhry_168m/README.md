# Dhrystone 2.1 @ 168 MHz — dev1 (STM32F407VET6)

Classic Dhrystone 2.1 (dhry_1.c / dhry_2.c / dhry.h), **2,000,000 runs**, on
the **dev1** board (custom STM32F407VET6) clocked at **168 MHz** (HSE 25 MHz,
PLL M=25 N=336 P=2 → SYSCLK). Compiler-agnostic: the same sources build with
either **GNU arm-none-eabi-gcc** or **Keil Arm Compiler 6 (armclang)**,
selected at configure time.

## Results (measured on hardware, 168 MHz, hard-float)

Normal toolchain comparison (no LTO):

| Toolchain                | Flags                                    | Dhrystones/s | DMIPS/MHz |
| ------------------------ | ---------------------------------------- | ------------ | --------- |
| GCC 15.3.1               | `-Ofast -ffp-contract=fast -funroll-loops` | 351,370     | 1.190     |
| armclang 6.24 (Keil AC6) | `-Ofast -ffp-contract=fast -funroll-loops` | 393,391     | 1.333     |

All builds print correct final values (Int_Glob=5, Arr_2_Glob = runs+10, …).

> ⚠ **Do not use LTO for Dhrystone.** GCC `-flto` sees the whole program and
> hoists the loop-invariant work out of the timed loop, inflating the score
> ~2.2× (to 770,713 Dhrystones/s) while still passing the final-value check.
> The LTO number is meaningless and is **excluded from the comparison above**.
> Full explanation and reproduction: **`LTO_on_dhrystone.md`** in this folder.

armclang is ~12 % faster than plain GCC here, but see the toolchain note in
the board-level `../../README.md`: **Arm Compiler for Embedded 6.24 is the
final feature release** (defect fixes only), and the whole armclang/GNU-ld/newlib
mixing needed two shims (`../../cmake/printf_rename.h`,
`../../cmake/armclang_force_wint_t.h`).
For new work prefer the open LLVM Embedded Toolchain or GNU gcc.

## Build

Requires the CMake/Ninja environment from the board-level `../../README.md`.

```bash
# armclang (default)
mkdir -p build && cd build
cmake -G Ninja ..
ninja
ninja flash          # programs the board via probe-rs / ULINK2 (SWD)

# GNU gcc (same build dir works for armclang too)
cmake -G Ninja -DSTM32_TOOLCHAIN=gcc ..
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

Results are printed over **USART3 (PD8 TX / PD9 RX, 115200 8-N-1)** to the
on-board RS232/485 transceiver. Read with any USB-serial adapter
(example capture script in the board-level `../../README.md`).
