# CoreMark 1.0.1 benchmark running the timed kernel from SRAM (0x20000000)
# on the nano-f411 (STM32F411CEU6) @ 100 MHz.
# The benchmark core (core_*.c) is linked into SRAM via a project linker
# script and copy-in'd from flash at startup; harness/HAL/printf stay in flash.
# Timed with SysTick (HAL_GetTick, 1 kHz).
# The F411 has a single 128 KB SRAM block (no SRAM1/SRAM2 split like the F407);
# executing the kernel from plain SRAM (no ART I-cache) is the motivation for
# this run -- see README.
#
# Pico-style workflow (gcc default; armclang / starm-clang for alternate CCs):
#   mkdir -p build && cd build
#   cmake -G Ninja ..                    # gcc
#   cmake -G Ninja -DSTM32_TOOLCHAIN=armclang ..   # Keil AC6
#   cmake -G Ninja -DSTM32_TOOLCHAIN=starm-clang .. # ST LLVM
#   ninja
#   ninja flash

cmake_minimum_required(VERSION 3.13)
if(POLICY CMP0123)
    cmake_policy(SET CMP0123 NEW)
endif()

set(STM32_TOOLCHAIN "gcc" CACHE STRING "C compiler: gcc (default), armclang (Keil AC6), or starm-clang (ST LLVM)")
set_property(CACHE STM32_TOOLCHAIN PROPERTY STRINGS gcc armclang starm-clang)
option(STM32_LTO "Enable LTO (gcc or starm-clang only)" OFF)

if(NOT CMAKE_TOOLCHAIN_FILE)
    if(STM32_TOOLCHAIN STREQUAL "gcc")
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
    elseif(STM32_TOOLCHAIN STREQUAL "armclang")
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/armclang-keil-toolchain.cmake")
    else()
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/starm-clang-toolchain.cmake")
    endif()
endif()

project(coremark_sram C ASM)

if(STM32_TOOLCHAIN STREQUAL "armclang")
    include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/armclang-postproject.cmake)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(BENCH_OPT "-Ofast -ffp-contract=fast -funroll-loops" CACHE STRING
    "Optimization flags for stm32f411_apply_board")
set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/coremark_1_0_1)

# Run the benchmark core from SRAM (see linker script in this dir).
set(BOARD_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/stm32f411coremark_sram.ld)

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/utils.c
    src/core_portme.c
    ${CORE}/core_list_join.c
    ${CORE}/core_main.c
    ${CORE}/core_matrix.c
    ${CORE}/core_state.c
    ${CORE}/core_util.c
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f411_board.cmake)
stm32f411_apply_board(${PROJECT_NAME}.elf "${BENCH_OPT}")

target_include_directories(${PROJECT_NAME}.elf PRIVATE
    src
    ${CORE}
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)
