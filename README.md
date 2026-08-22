# F4 demo (multi-board)

Bare-metal firmware projects and tooling for multiple **STM32F4-series**
boards/chips. Each board lives in its own folder with a self-contained
`board/` (clock, LEDs, console, startup, linker script), `cmake/` (toolchain +
board helpers) and `app/` (projects). Board-level docs — hardware, clock tree,
build/flash/console — live in each board folder's README.

## Boards

| Board            | What it is                                          |
| ---------------- | --------------------------------------------------- |
| `dev1-f407/`     | Custom STM32F407VET6 @ 168 MHz, 25 MHz HSE, 3 LEDs, USART3 console (see its README) |
| `nano-f407/`     | STM32F407VET6 @ 168 MHz, 8 MHz HSE, 1 LED (PB0), USART1 → ST-Link VCP console (see its README) |

`dev1-f407/` has the blink demo and the Dhrystone / CoreMark benchmark
projects (GCC **or** Keil AC6 armclang toolchain, `-DSTM32_TOOLCHAIN=gcc|armclang`
at configure time). `nano-f407/` has a `blink_hello` demo that also samples
the ADC1 internal channels (temperature / VREFINT / VBAT), plus the same two
benchmarks.

The `-<chip>` suffix in board folder names keeps it a multi-board/**multi-chip**
repo: e.g. a `nano-f411` or `nano-f446` would sit next to `nano-f407`.

## Repo map

| Path              | What it is                                       |
| ----------------- | ------------------------------------------------ |
| `dev1-f407/`      | Board + all its projects (see `dev1-f407/README.md`) |
| `nano-f407/`      | Board + all its projects (see `nano-f407/README.md`) |
| `dev1-f407/app/`  | Applications (blink, Dhrystone, CoreMark)        |
| `nano-f407/app/`  | Applications (blink_hello + ADC, Dhrystone, CoreMark) |
| `*/board/`        | Per-board shared layer (clock, UART, startup, linker) |
| `*/cmake/`        | Per-board toolchain + board + flash-target helpers |

## Toolchain / environment

* GNU arm-none-eabi-gcc 15.3.1 (default) or Keil AC6 armclang (`-DSTM32_TOOLCHAIN=armclang`).
* CMake + Ninja (Pico-style; MSYS2 mingw64 `build.sh` adds them to `PATH`).
* probe-rs (SWD flashing) + OpenOCD (alternative).

Build + flash any project with:

```bash
cd dev1-f407/app/blink
bash build.sh        # or: mkdir build && cd build && cmake -G Ninja .. && ninja
ninja flash
```