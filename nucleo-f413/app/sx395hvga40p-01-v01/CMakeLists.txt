# ILI9488 320x480 LCD test ("sx395hvga40p-01-v01") for the "nucleo-f413"
# board (STM32F413ZHT6 @ 100 MHz). Drives the SX395HVGA40P-01-V01 3.5-inch
# ILI9488 module with a 4-wire SPI:
#   SCL = PA5, SDA = PA7, DC = PD15, RST = PF12, CS = PD14, BL = PE9
#   (TIM1_CH1 PWM).
# ILI9488 init (vendor-verbatim): MADCTL 0x08, 16bpp COLMOD, power/VCOM/
# gamma registers; 320x480 windows at COL_Pre=ROW_Pre=0.
# Two drive methods like st7365_md350_320x480: soft bit-bang and HW SPI1.
#
# Pico-style workflow:
#   bash build.sh          # == mkdir build && cd build && cmake -G Ninja .. && ninja
#   ninja flash            # programs the board (probe-rs, ST-Link SWD)

cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
endif()

project(sx395hvga40p-01-v01 C ASM)

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

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f413_board.cmake)
stm32f413_apply_board(${PROJECT_NAME}.elf "-O1")

target_include_directories(${PROJECT_NAME}.elf PRIVATE
    src
    src/init
    src/blockwrite
)


include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)