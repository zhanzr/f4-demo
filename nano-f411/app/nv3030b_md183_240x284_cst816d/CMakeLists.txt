# nv3030b_md183_240x284_cst816d - 1.83" 240x284 NV3030B LCD + CST816D touch
# for the "nano-f411" board (STM32F411CEU6 @ 100 MHz).
#
# The NV3030B runs its QSPI-compatible single-lane serial protocol over
# normal SPI pins (the module does not expose QSPID2/D3 for this wiring):
#   SCL = PA5, SDA/MOSI = PA7, CS = PA4
#   "DC/MISO" = PA6 - NOT USED (the wrapped-command framing carries
#   command/data; the vendor example defines it as DC but never drives it)
# Touch (CST816D, bit-banged I2C - the F411 has no I2C on PA2/PA3):
#   TOUCH_SCL = PA2, TOUCH_SDA = PA3 (open-drain)
#
# Pico-style workflow:
#   bash build.sh          # == mkdir build && cd build && cmake -G Ninja .. && ninja
#   ninja flash            # programs the board (probe-rs, ST-Link SWD)

cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
endif()

project(nv3030b_md183_240x284_cst816d C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/interface.c
    src/lcd.c
    src/touch.c
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