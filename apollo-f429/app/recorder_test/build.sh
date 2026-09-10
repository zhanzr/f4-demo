#!/usr/bin/env bash
# Build the apollo-f429 (STM32F429IGT6) stage-2 app: recorder_test (SDRAM at
# 0xC0000000). Pico-style: bash build.sh  ->  cmake -G Ninja .. && ninja
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