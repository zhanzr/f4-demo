# Dhrystone 2.1 @ 180 MHz — fire-f429 (STM32F429IGT6)

Classic Dhrystone 2.1 (dhry_1.c / dhry_2.c / dhry.h), **2,000,000 runs**, on
the **fire-f429** board (STM32F429IGT6, bare metal) clocked at **180 MHz**
(HSE 25 MHz, PLL M=25 N=360 P=2 → SYSCLK). Compiler-agnostic: the same sources
build with either **GNU arm-none-eabi-gcc** or **Keil Arm Compiler 6
(armclang)**, selected at configure time.

## Results (measured on hardware, 180 MHz, hard-float)

| Toolchain    | Flags                                      | Dhrystones/s | DMIPS/MHz |
| ------------ | ------------------------------------------ | ------------ | --------- |
| GCC 15.3.1   | `-Ofast -ffp-contract=fast -funroll-loops` | 391,236      | 1.237     |
| ARMCLANG (Keil AC6) | `-Ofast -ffp-contract=fast -funroll-loops` | **445,434** | **1.408** |
| ST Arm clang | `-Ofast -ffp-contract=fast -funroll-loops` | 444,346      | 1.405     |

Per toolchain, only the highest measured configuration is shown (armclang
`-Omax` measures the same 445,434 as `-Ofast` — applying the nano-f411
finding that `-Omax` adds nothing for Dhrystone). All runs print correct final
values (Int_Glob=5, Arr_2_Glob = runs+10, …).

> ⚠ **Do not use LTO for Dhrystone.** GCC `-flto` sees the whole program and
> hoists the loop-invariant work out of the timed loop, inflating the score
> while still passing the final-value check. The LTO number is meaningless and
> is **excluded from the table above**. Full explanation and reproduction:
> **`LTO_on_dhrystone.md`** in this folder.

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

# GNU gcc + LTO (kept only as reproducible evidence of the artifact — see LTO_on_dhrystone.md)
cmake -G Ninja -DSTM32_TOOLCHAIN=gcc -DSTM32_LTO=ON ..
ninja
```

Use a separate build dir per toolchain (`build/`, `build-gcc/`,
`build-gcc-lto/`) because `CMAKE_TOOLCHAIN_FILE` is cached after configure.

## Console

Results are printed over the board's console UART (see the board-level
`../../README.md` for the port/baud and a capture recipe).