#!/usr/bin/env bash
# Build the apollo-f429 (STM32F429IGT6) two-stage `app` firmware: stage-1
# bootloader (internal flash) + stage-2 app (SDRAM 0xC0000000, NAND-shimmed).
# Run with:  bash build.sh    (or ./build.sh on Linux)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Prefer the MSYS2 mingw64 environment (newer CMake/Ninja) when present.
if [ -d /mingw64/bin ] && ! command -v cmake >/dev/null 2>&1; then
    export PATH="/mingw64/bin:/usr/bin:$PATH"
fi

mkdir -p build
cd build
cmake -G Ninja "$@" ..
ninja