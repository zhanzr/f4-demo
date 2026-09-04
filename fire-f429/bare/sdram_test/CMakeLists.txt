# FMC SDRAM init/read/write test for the fire-f429 board (STM32F429IGT6).
# The test uses built-in flash/SRAM only; SDRAM is initialized at runtime and
# accessed directly at its FMC bank-1 address (no linker remapping).

cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
endif()

project(sdram_test C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}.elf
    src/main.c
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f429_board.cmake)
stm32f429_apply_board(${PROJECT_NAME}.elf "-O2")


include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)
