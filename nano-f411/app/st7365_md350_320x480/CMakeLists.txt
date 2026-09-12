# ST7365P 320x480 LCD test ("st7365_md350_320x480") for the "nano-f411"
# board (STM32F411CEU6 @ 100 MHz). Drives the MD350 3.5-inch ST7365P module
# with a 4-wire soft bit-banged SPI:
#   SCL = PA5, SDA = PA7, RES = PA3, DC = PA4, CS = PB8, BL = PB9 (TIM4_CH4,
#   15% PWM backlight). MISO = PA6 exists on the module but is not used by
#   the write-only soft driver (maps to SPI1_MISO at AF5 for a future HW
#   SPI1 path).
# ST7365P (ST7796-register-compatible) init: MADCTL 0x48 portrait, 16bpp
# COLMOD, power/VCOM/gamma registers; 320x480 windows at COL_Pre=ROW_Pre=0.
#
# Pico-style workflow:
#   bash build.sh          # == mkdir build && cd build && cmake -G Ninja .. && ninja
#   ninja flash            # programs the board (probe-rs, ST-Link SWD)

cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
endif()

project(st7365_md350_320x480 C ASM)

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