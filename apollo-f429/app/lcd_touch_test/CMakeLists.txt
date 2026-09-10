# lcd_touch_test - apollo-f429 two-stage NAND boot stage-2 LCD + touch app (SDRAM).
# Same driver + test set as bare/lcd_touch_test, but as the stage-2 SDRAM app:
# linked at 0xC0000000 (SDRAM), booted there by the app/tool bootloader, using
# the non-destructive SystemInit. The 2.8" TFT is on the 16-bit FSMC parallel
# bus (ILI9341, 0x9341); the vendored ALIENTEK touch stack (GT9147/FT5206/
# OTT2001A + XPT2046-style resistive + AT24C02 calibration) runs as-is.
#
# Shares the driver sources with bare/lcd_touch_test (single copy, like the
# h723 boot reusing spi_flash_test's w25q64.c).
#
# Pico-style workflow:
#   bash build.sh            # == cmake -G Ninja .. && ninja
#   ninja flash              # writes app.hex -> NAND via the FMC-NAND algo,
#                            # then resets (app/boot loads + runs it)
#   ninja flash-boot         # (helper) programs tool/boot into internal flash too

cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
endif()

project(lcd_touch_test C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Stage-2 app: link entirely in SDRAM at 0xC0000000 and use the non-destructive
# SystemInit (the bootloader owns the clock tree).
set(STM32_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/app.ld" CACHE FILEPATH
    "stage-2 app linker script (SDRAM at 0xC0000000)" FORCE)
set(STM32F429_SYSTEM_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/system_app.c" CACHE FILEPATH
    "stage-2 app system init (non-destructive)" FORCE)

set(BARE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../bare/lcd_touch_test/src")

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/system_app.c
    ${BARE_SRC}/lcd.c
    ${BARE_SRC}/delay.c
    ${BARE_SRC}/math_shim.c
    ${BARE_SRC}/adc_internal.c
    ${BARE_SRC}/dht11.c
    ${BARE_SRC}/pcf8574.c
    ${BARE_SRC}/fonts/lcd_fonts.c
    ${BARE_SRC}/fonts/lcd_font_1608.c
    ${BARE_SRC}/vendor/touch/touch.c
    ${BARE_SRC}/vendor/touch/ctiic.c
    ${BARE_SRC}/vendor/touch/gt9147.c
    ${BARE_SRC}/vendor/touch/ft5206.c
    ${BARE_SRC}/vendor/touch/ott2001a.c
    ${BARE_SRC}/vendor/eeprom/24cxx.c
    ${BARE_SRC}/vendor/eeprom/myiic.c
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f429_board.cmake)
stm32f429_apply_board(${PROJECT_NAME}.elf "-O1")
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE NAND_APP=1)

target_include_directories(${PROJECT_NAME}.elf PRIVATE
    ${BARE_SRC}
    ${BARE_SRC}/fonts
    ${BARE_SRC}/vendor/touch
    ${BARE_SRC}/vendor/eeprom
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/nand-flash-targets.cmake)