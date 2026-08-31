# AGENTS.md

Guidance for AI agents (and humans) working in this repo. Keep it brief; the
authoritative docs already live in each board's `README.md` and the root
`README.md`.

## What this repo is

Bare-metal firmware for **STM32F4-series** boards. It is a **multi-board /
multi-chip** repo: each board lives in its own `<name>-<chip>` folder
(`dev1-f407`, `nano-f407`, `fire-f429`) and is fully self-contained:

- `board/` — clock init, LEDs, console, startup assembly, linker script,
  `stm32f4xx_hal_conf.h`, `stm32f4xx_hal_msp.c`, `stm32f4xx_it.c`, newlib stubs.
- `cmake/` — toolchain files, the board-apply helper, flash targets, OpenOCD cfg.
- `app/` (and `bare/` in fire-f429) — the actual projects; each has a
  `build.sh`, a `CMakeLists.txt`, and a `src/` directory.
- Vendored HAL/CMSIS lives in the **repo root** `drivers/` (all boards share it).

Per-board hardware/clock/console facts differ — **always read the board's own
`README.md`** before touching or writing a project for it (e.g. HSE crystal is
25 MHz on `dev1-f407` but 8 MHz on `nano-f407`; console is SWV/ITM on
`dev1-f407` but USART1-VCP on `nano-f407`).

## Build & flash workflow

Pico-style **CMake + Ninja**, run per-project. From a project dir
(`app/blink_hello`, etc.):

```bash
bash build.sh        # == mkdir -p build && cd build && cmake -G Ninja .. && ninja
ninja flash          # probe-rs download + reset over SWD
ninja flash-ocd      # alternative: flash via OpenOCD
ninja swv            # stream SWV/ITM printf output (dev1-f407; Ctrl-C to stop)
```

The `build/` directories under each project are **generated** (and git-ignored);
do not hand-edit them.

- Toolchain: GNU arm-none-eabi-gcc (default) **or** Keil AC6 `armclang` via
  `-DSTM32_TOOLCHAIN=armclang`. CMake/Ninja come from the MSYS2 mingw64
  environment (added to `PATH` by `build.sh` when missing).
- Flash uses **probe-rs** by default (auto-detects the probe); OpenOCD is the
  alternative. Pin a specific probe with `-DDEBUG_PROBE=<selector>` at configure
  time.

## Project structure conventions

When adding a new project, follow the pattern of an existing one
(e.g. `dev1-f407/app/blink_hello`):

- `CMakeLists.txt`:
  - `cmake_minimum_required(VERSION 3.13)`; set `CMAKE_TOOLCHAIN_FILE` to
    `../../cmake/arm-none-eabi-toolchain.cmake` if not already set.
  - `project(<name> C ASM)` — **ASM is required** (startup `.s`).
  - `add_executable(<name>.elf src/... )`.
  - `include(../../cmake/stm32f407_board.cmake)` (or the f429 variant) and call
    the board-apply function with the optimization flag, e.g.
    `stm32f407_apply_board(${PROJECT_NAME}.elf "-O1")`.
  - Add the objcopy `.hex`/`.bin` POST_BUILD command and
    `include(../../cmake/flash-targets.cmake)`.
- `build.sh`: copy the existing one verbatim (it is generic).
- `README.md`: document hardware, build, flash, console.

Board code lives in `board/`, **not** per-project. New shared helper source
belongs there and is pulled in by the board-apply CMake function.

## Conventions & gotchas

- HAL vendor tree is shared across boards in root `drivers/`; `STM32F4_HAL_ROOT`
  defaults to `../../drivers`. To use the full STM32Cube_FW_F4 package instead,
  pass `-DSTM32F4_HAL_ROOT` at configure time.
- Compile definitions set by the board layer: `STM32F407xx` (or the F429 part)
  and `USE_HAL_DRIVER`.
- `-ffunction-sections -fdata-sections` + `--gc-sections` are on by default;
  benchmarks add aggressive flags like `-Ofast -ffp-contract=fast -funroll-loops`.
- **LTO** (`-DSTM32_LTO=ON`) is GCC-only; armclang ignores it (LLVM bitcode
  cannot be consumed by GNU ld). When LTO is on, `syscalls.c` is forcibly
  compiled `-fno-lto` — do not remove that.
- Console output goes through `swv_printf`/`uart_printf` (the board layer), not
  `printf` directly.
- Do not add code comments beyond what's needed; the existing sources use
  sparse explanatory comments only where non-obvious (e.g. LTO/syscalls workaround).
- There is no test suite in this repo; verification is by building + flashing
  a target board (or just `bash build.sh` to confirm it compiles clean).

## e_server / web assets

Some `eth_http_server` projects embed a small HTTP server. The `e_server/`
folder holds the web frontend sources; the embedded C array is generated with
`python build_web.py` (see `.vscode/settings.json` for the canonical command).
If you change the web frontend, regenerate the asset header for the affected
project rather than editing the `.h` by hand.
