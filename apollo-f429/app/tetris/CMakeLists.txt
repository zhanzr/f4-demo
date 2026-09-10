# tetris - apollo-f429 two-stage NAND boot stage-2 Tetris game (SDRAM).
# Ported from D:/c5_prj/nucleo-c542/board_t1/tetris (ST7789 240x240) to the
# apollo-f429 2.8" ILI9341 240x320 (16-bit FSMC parallel). Input:
#   touch button (TPAD, TIM2-ch1/PA5) = start/pause/resume
#   KEY_UP (PA0, pull-down, active high) = rotate
#   KEY_2  (PC13, pull-up,  active low)  = left
#   KEY_1  (PH2,  pull-up,  active low)  = drop
#   KEY_0  (PH3,  pull-up,  active low)  = right
# Top-5 ranks live in the on-board AT24C02 EEPROM (I2C2) with a
# magic/version/CRC validity check (board/ee_flash.c).
#
# Pico-style workflow:
#   bash build.sh            # == cmake -G Ninja .. && ninja
#   ninja flash              # -> NAND via the FMC-NAND algo, then reset
#   ninja flash-boot         # (helper) programs tool/boot into internal flash too

cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
endif()

project(tetris C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Stage-2 app: link entirely in SDRAM at 0xC0000000 and use the non-destructive
# SystemInit (the bootloader owns the clock tree).
set(STM32_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/app.ld" CACHE FILEPATH
    "stage-2 app linker script (SDRAM at 0xC0000000)" FORCE)
set(STM32F429_SYSTEM_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/system_app.c" CACHE FILEPATH
    "stage-2 app system init (non-destructive)" FORCE)

# Shared board drivers referenced from the bare LCD project (single copy) +
# the board-layer AT24C02 EEPROM driver.
set(BARE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../bare/lcd_touch_test/src")

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/tpad.c
    src/key.c
    ${BARE_SRC}/lcd.c
    ${BARE_SRC}/delay.c
    ${BARE_SRC}/fonts/lcd_fonts.c
    ${BARE_SRC}/fonts/lcd_font_1608.c
    ../../board/ee_flash.c
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f429_board.cmake)
stm32f429_apply_board(${PROJECT_NAME}.elf "-O1")
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE NAND_APP=1)

target_include_directories(${PROJECT_NAME}.elf PRIVATE
    src
    ${BARE_SRC}
    ${BARE_SRC}/fonts
    ../../board
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/nand-flash-targets.cmake)