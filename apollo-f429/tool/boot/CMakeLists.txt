# boot - apollo-f429 stage-1 bootloader (internal flash).
# Two-stage NAND boot: brings up 180 MHz clocks / console / SDRAM / NAND,
# copies the stage-2 app image from NAND offset 0 to SDRAM 0xC0000000, validates
# it and jumps. One-time per board; the stage-2 app then lives on the NAND.
#
# Pico-style workflow:
#   bash build.sh            # == mkdir build && cd build && cmake -G Ninja .. && ninja
#   ninja flash              # internal flash via OpenOCD (ULINK2, SWD)
#   ninja flash-probe        # internal flash via probe-rs

cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
endif()

project(boot C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/nand.c
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f429_board.cmake)
stm32f429_apply_board(${PROJECT_NAME}.elf "-O1")

# FMC NAND HAL driver (HAL_NAND_Init / HAL_NAND_MspInit callbacks in nand.c).
target_sources(${PROJECT_NAME}.elf PRIVATE
    ${STM32F4_HAL_SRC}/stm32f4xx_hal_nand.c
)

# The board layer's default linker script (internal flash) is used as-is.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)