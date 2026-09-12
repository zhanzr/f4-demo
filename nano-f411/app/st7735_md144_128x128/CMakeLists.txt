# ST7735S 128x128 LCD test ("st7735_md144_128x128") for the "nano-f411" board
# (STM32F411CEU6 @ 100 MHz). Drives the MD-144 1.44-inch ST7735S module with a
# 4-wire bit-banged SPI, wiring swapped vs the vendor example (C8T6_md144_t1):
#   SCL = PA5, SDA = PA7, RES = PA6, DC = PA4, CS = PB8, BL = PB9 (TIM4_CH4,
#   ~20% PWM backlight).
# Test patterns ported from the c542 board_t1 st7789_md130_240x240 demo
# (banner + info pages + TEST_STAND + HSV gradient + LED test, FPS counter).
#
# Pico-style workflow:
#   bash build.sh          # == mkdir build && cd build && cmake -G Ninja .. && ninja
#   ninja flash            # programs the board (probe-rs, ST-Link SWD)

cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
endif()

project(st7735_md144_128x128 C ASM)

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