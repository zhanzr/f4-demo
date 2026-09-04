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
| `fire-f429/`     | STM32F429IGT6 @ 180 MHz, 1 MB flash / 256 KB SRAM, 4 LEDs (PH10/11/12, PD12), USART1 → USB-serial VCP console (see its README) |

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

* GNU arm-none-eabi-gcc 15.3.1 (default) or Keil AC6 armclang (`-DSTM32_TOOLCHAIN=armclang`); ST Arm clang, where supported (F407/F411 benchmark projects).
* CMake + Ninja (Pico-style; MSYS2 mingw64 `build.sh` adds them to `PATH`).
* probe-rs (SWD flashing) + OpenOCD (alternative).

## Build configuration

Configure and build each project from its own directory
(`<board>/app/<project>` or `<board>/bare/<project>`). No absolute paths are
needed — the build directory is a relative subfolder of the project.

```bash
cd nano-f411/app/blink_hello

# default: gcc + the project's hard-coded optimization level
bash build.sh          # == mkdir -p build && cd build && cmake -G Ninja .. && ninja
ninja flash            # program via probe-rs (ST-Link SWD)

# other toolchains: use one build dir per toolchain
# (CMAKE_TOOLCHAIN_FILE is cached at configure time)
cmake -G Ninja -S . -B build-armclang -DSTM32_TOOLCHAIN=armclang
ninja -C build-armclang
cmake -G Ninja -S . -B build-starm-clang -DSTM32_TOOLCHAIN=starm-clang
ninja -C build-starm-clang
```

**Optimization levels** are passed to the board-apply CMake function in each
project's `CMakeLists.txt`:

* **General projects** need nothing on the command line — their optimization is
  hard-coded at a sane level: `-O1` for simple demos (`blink_hello`,
  `ram_test`), `-O2`/`-O3` for peripheral-heavy apps.
* **Benchmark projects** (`coremark_*`, `dhry_*`) default to aggressive flags
  and let you override them at configure time with `-DBENCH_OPT="..."` (plus
  `-DBENCH_OPT_C="..."` for C-only flags such as armclang `-Omax`). The
  fastest measured per-toolchain settings are documented in each benchmark's
  README.