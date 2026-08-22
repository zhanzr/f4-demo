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
| `fire-f429/`     | STM32F429 placeholder (see its README)             |

The `-<chip>` suffix in board folder names keeps it a multi-board/**multi-chip**
repo: e.g. a `nano-f411` or `nano-f446` would sit next to `nano-f407`, and a
`fire-f429` board would sit next to the F407 boards.

## Vendored HAL / CMSIS

The STM32F4 **HAL driver + CMSIS** are vendored in the repo root `drivers/`
(trimmed subset of the official `STM32Cube_FW_F4` package):

```
drivers/
├── CMSIS/
│   ├── Include/                        CMSIS core headers
│   └── Device/ST/STM32F4xx/Include/   STM32F4 device headers
└── STM32F4xx_HAL_Driver/
    ├── Inc/ (+ Legacy)                 HAL headers
    └── Src/ (all HAL modules)          HAL sources
```

Builds use this by default (`-DSTM32F4_HAL_ROOT` defaults to `../../drivers`).
The trimmed tree covers everything these projects compile; it does **not**
include the BSP, DSP, middleware, projects, docs, or `.chm` manuals.

To use the **full** official package instead (e.g. to pull in something not
vendored), point `STM32F4_HAL_ROOT` at its root when configuring:

```bash
cmake -G Ninja -DSTM32F4_HAL_ROOT="C:/Users/user1/STM32Cube/Repository/STM32Cube_FW_F4_V1.28.3" ..
```

The full package is STM32CubeMX → "Manage embedded software packages" →
STM32Cube MCU Package → "STM32Cube FW_F4 V1.28.3", installed under
`<STM32Cube>/Repository/STM32Cube_FW_F4_V1.28.3`.

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