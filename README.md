# STM32 firmware projects (multi-board)

Bare-metal firmware projects and tooling for multiple STM32 boards. Each board
lives in its own folder with a self-contained `board/` (clock, LEDs, console,
startup, linker script), `cmake/` (toolchain + board helpers) and `app/`
(projects). Board-level docs — hardware, clock tree, build/flash/console —
live in each board folder's README.

## Boards

| Board            | What it is                                          |
| ---------------- | --------------------------------------------------- |
| `dev1/`          | Custom STM32F407VET6 @ 168 MHz, 25 MHz HSE, 3 LEDs, USART3 console (see its README) |
| `nano/`          | STM32F407VET6 @ 168 MHz, 8 MHz HSE, 1 LED (PB0), USART1 → ST-Link VCP console (see its README) |

`dev1/` contains the blink demo and the Dhrystone / CoreMark benchmark
projects (GCC **or** Keil AC6 armclang toolchain, `-DSTM32_TOOLCHAIN=gcc|armclang`
at configure time). `nano/` has a `blink_hello` demo that also samples the
ADC1 internal channels (temperature / VREFINT / VBAT), plus the same two
benchmarks.

## Repo map

| Path          | What it is                                       |
| ------------- | ------------------------------------------------ |
| `dev1/`       | Board + all its projects (see `dev1/README.md`)  |
| `nano/`       | Board + all its projects (see `nano/README.md`)  |
| `dev1/app/`   | Applications (blink, Dhrystone, CoreMark)        |
| `nano/app/`   | Applications (blink_hello + ADC, Dhrystone, CoreMark) |
| `*/board/`    | Per-board shared layer (clock, UART, startup, linker) |
| `*/cmake/`    | Per-board toolchain + board + flash-target helpers |

## Toolchain / environment

* GNU arm-none-eabi-gcc 15.3.1 (default) or Keil AC6 armclang (`-DSTM32_TOOLCHAIN=armclang`).
* CMake + Ninja (Pico-style; MSYS2 mingw64 `build.sh` adds them to `PATH`).
* probe-rs (SWD flashing) + OpenOCD (alternative).

Build + flash any project with:

```bash
cd dev1/app/blink
bash build.sh        # or: mkdir build && cd build && cmake -G Ninja .. && ninja
ninja flash
```
