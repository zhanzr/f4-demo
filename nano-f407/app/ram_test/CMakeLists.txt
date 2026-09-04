# Minimal RAM-execution proof for the dev1-f407 board (STM32F407VET6) @ 168 MHz.
# Links one function (ram_hello) into SRAM2 via a project linker script and
# copy-in's it from flash at startup; harness/HAL/reset stay in flash. Used to
# isolate whether executing code from SRAM2 works at all (CoreMark-in-SRAM's
# hang is the real question).
#
# Pico-style workflow:
#   mkdir -p build && cd build
#   cmake -G Ninja ..
#   ninja           # builds .elf / .hex / .bin
#   ninja flash     # programs the board (probe-rs, ULINK2 CMSIS-DAP / SWD)

cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
endif()

project(ram_test C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(BOARD_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/stm32f407ram_test_ccm.ld)

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/ram_func.c
    src/ccm_function.c
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f407_board.cmake)
stm32f407_apply_board(${PROJECT_NAME}.elf "-O1")

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)
