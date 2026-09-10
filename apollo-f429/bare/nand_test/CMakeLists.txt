# FMC NAND read/erase/write test for the apollo-f429 board (STM32F429IGT6).
# On-board NAND (MT29F16G08ABABA / MT29F4G08ABADA) on FMC NAND bank 3
# (NCE3 = PG9, 0x80000000), 8-bit data, R/B on PD6. No linker remapping:
# SDRAM/NAND are only accessed at runtime by this test.
# The board layer doesn't compile stm32f4xx_hal_nand.c, so it's added here.

cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
endif()

project(nand_test C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/nand.c
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f429_board.cmake)
stm32f429_apply_board(${PROJECT_NAME}.elf "-O1")

# FMC NAND HAL driver (HAL_NAND_Init / HAL_NAND_MspInit callbacks).
target_sources(${PROJECT_NAME}.elf PRIVATE
    ${STM32F4_HAL_SRC}/stm32f4xx_hal_nand.c
)


include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)