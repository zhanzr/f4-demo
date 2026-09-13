# JD9858 (DS: JD5858) 1.3" 360x360 round LCD + CHSC6417 touch for the
# STM32F407VET6 "nano-f407" board. The panel sits on the FSMC (NE1,
# 8-bit data bus, DC on A16) - the same wiring as the vendor's
# F103VET6 example, which the F407VET6 maps identically.
#
# Pico-style workflow:
#   mkdir -p build && cd build
#   cmake -G Ninja ..
#   ninja           # builds .elf / .hex / .bin
#   ninja flash     # programs the board (probe-rs, ST-Link SWD)

cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
endif()

project(jd9858_md130_360x360_chsc6417 C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/lcd.c
    src/interface.c
    src/touch.c
    src/backlight.c
    src/lcd/lcd_fonts.c
    src/lcd/lcd_font_1608.c
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f407_board.cmake)
stm32f407_apply_board(${PROJECT_NAME}.elf "-O1")

# FSMC NOR/SRAM backend for the panel (HAL_SRAM + its LL layer).
target_sources(${PROJECT_NAME}.elf PRIVATE
    ${STM32F4_HAL_SRC}/stm32f4xx_hal_sram.c
    ${STM32F4_HAL_SRC}/stm32f4xx_ll_fsmc.c
)

target_include_directories(${PROJECT_NAME}.elf PRIVATE
    src
    src/blockwrite
)


include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)
