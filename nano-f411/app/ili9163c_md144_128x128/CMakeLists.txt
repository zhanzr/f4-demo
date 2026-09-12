# ILI9163C 128x128 LCD test ("ili9163c_md144_128x128") for the "nano-f411"
# board (STM32F411CEU6 @ 100 MHz). Drives the MD144-form-factor ILI9163C
# module over a 3-wire serial bus (9-bit frames: D/C bit + 8 data bits - the
# module connector has no D/C pin) with the soft bit-banged method:
#   SCL = PA5, SDA = PA7, RES = PA6, CS = PB8
# The module's backlight is hardwired on-board (no BL control).
#
# Pico-style workflow:
#   bash build.sh          # == mkdir build && cd build && cmake -G Ninja .. && ninja
#   ninja flash            # programs the board (probe-rs, ST-Link SWD)

cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
endif()

project(ili9163c_md144_128x128 C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/interface.c
    src/lcd.c
    src/lcd/lcd_fonts.c
    src/lcd/lcd_font_1608.c
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f411_board.cmake)
stm32f411_apply_board(${PROJECT_NAME}.elf "-O1")

target_include_directories(${PROJECT_NAME}.elf PRIVATE
    src
    src/init
    src/blockwrite
)


include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)