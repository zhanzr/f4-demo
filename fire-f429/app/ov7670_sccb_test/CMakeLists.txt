# ov7670_sccb_test: minimal SCCB bring-up for the OV7670 on the fire-f429.
#
# Step 1 of the OV7670 bring-up: verify the SCCB (I2C) link with ONLY the
# minimal signal set, exactly as the user specified:
#
#   Power, SCL/SDA (I2C1 PB6/PB7), PWDN (PG3), RST (PG2), XCLK (PA8 MCO1)
#
# The OV7670's SCCB/register logic is CLOCKED BY XCLK (unlike the OV5640's
# on-module crystal), so XCLK must be present for SCCB to answer at all.
# The DCMI data pins (D0..D7, PCLK, HSYNC, VSYNC) are NOT touched here.
#
# It tries multiple bring-up strategies and prints whether the OV7670 ACKs:
#   1. hardware I2C (I2C1 400 kHz + 100 kHz) with XCLK 25/12.5 MHz
#   2. a bit-banged SCCB on the same PB6/PB7 pins (ALIENTEK-style, tolerant
#      of marginal wiring; no dependency on the I2C peripheral)
#   3. RST/PWDN polarity sweep (some modules strap these inverted)
#   4. full 7-bit address scan (the OV7670 might not be at 0x21)
#
# Console output only (USART1 115200) - NO LCD, NO DCMI, minimal pins. Same
# build/flash recipe as jpeg_test:
#
#   cmake -G Ninja -B build .      # gcc
#   ninja -C build
#   ninja -C build flash
cmake_minimum_required(VERSION 3.13)

set(STM32_TOOLCHAIN "gcc" CACHE STRING "C compiler: gcc (default) or armclang (Keil AC6)")
set_property(CACHE STM32_TOOLCHAIN PROPERTY STRINGS gcc armclang)

if(NOT CMAKE_TOOLCHAIN_FILE)
    if(STM32_TOOLCHAIN STREQUAL "armclang")
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/armclang-keil-toolchain.cmake")
    else()
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
    endif()
endif()

project(ov7670_sccb_test C ASM)

if(STM32_TOOLCHAIN STREQUAL "armclang")
    include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/armclang-postproject.cmake)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# .data/.bss in internal SRAM; no SDRAM used in this console-only probe.
set(STM32_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../../board/stm32f429igt6.ld")

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/sccb_bitbang.c
)
target_include_directories(${PROJECT_NAME}.elf PRIVATE ../../board src)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f429_board.cmake)
stm32f429_apply_board(${PROJECT_NAME}.elf "-O2")

add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex ${PROJECT_NAME}.elf ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary ${PROJECT_NAME}.elf ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROJECT_NAME}.hex ${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.hex
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROJECT_NAME}.bin ${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} ${PROJECT_NAME}.elf
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "objcopy -> .hex/.bin, size")
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)