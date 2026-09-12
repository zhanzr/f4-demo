# ST7789V 240x240 LCD test ("st7789_md130_240x240") for the "nano-f411"
# board (STM32F411CEU6 @ 100 MHz). Drives the MD130 1.3-inch ST7789V module
# with a 4-wire SPI:
#   SCL = PA5, SDA = PA7, RES = PA6, DC = PA4, CS = PB8, BL = PB9 (TIM4_CH4,
#   15% PWM soft pass / 13% HW pass).
# ST7789V init (MADCTL 0x00, IPS INVON, power/porch/gamma) and 240x240
# glass-at-GRAM-rows-0..239 geometry (COL_Pre = ROW_Pre = 0); dual-bus demo
# (soft bit-bang + HW SPI1 @ 25 MHz).
#
# Pico-style workflow:
#   bash build.sh          # == mkdir build && cd build && cmake -G Ninja .. && ninja
#   ninja flash            # programs the board (probe-rs, ST-Link SWD)

cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
endif()

project(st7789_md130_240x240 C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/interface.c
    src/lcd.c
    src/lcd/lcd_fonts.c
    src/lcd/lcd_font_1608.c
    src/backlight.c
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f411_board.cmake)
stm32f411_apply_board(${PROJECT_NAME}.elf "-O1")

target_include_directories(${PROJECT_NAME}.elf PRIVATE
    src
    src/init
    src/blockwrite
)


include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)