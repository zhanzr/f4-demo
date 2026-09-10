# lcd_touch_test - apollo-f429 2.8" TFT LCD (16-bit FSMC parallel) + touch test.
# Port of the vendored ALIENTEK Apollo tft_lcd_test + touch/实验30 examples:
#   - ILI9341 (0x9341) on FMC NOR/SRAM bank1 NE1, 16-bit, A18=RS (LCD_BASE 0x60000000|0x7FFFE)
#   - LCD ID read + vendor TEST loop, st7789-style test patterns (info page,
#     TEST_STAND screens, HSV gradient, LED, FPS), and full vendor touch
#     (GT9147/FT5206/OTT2001A + XPT2046-style resistive + AT24C02 calibration).
# Backlight = PB5, console USART1 (board layer), LEDs PB1/PB0.
#
# Pico-style workflow:
#   bash build.sh            # == cmake -G Ninja .. && ninja
#   ninja flash              # OpenOCD (ULINK2, SWD)
#   ninja flash-probe        # probe-rs

cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
endif()

project(lcd_touch_test C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/lcd.c
    src/delay.c
    src/math_shim.c
    src/adc_internal.c
    src/dht11.c
    src/fonts/lcd_fonts.c
    src/fonts/lcd_font_1608.c
    src/vendor/touch/touch.c
    src/vendor/touch/ctiic.c
    src/vendor/touch/gt9147.c
    src/vendor/touch/ft5206.c
    src/vendor/touch/ott2001a.c
    src/vendor/eeprom/24cxx.c
    src/vendor/eeprom/myiic.c
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f429_board.cmake)
stm32f429_apply_board(${PROJECT_NAME}.elf "-O1")

target_include_directories(${PROJECT_NAME}.elf PRIVATE
    src
    src/fonts
    src/vendor/touch
    src/vendor/eeprom
)

# The vendored touch/eeprom sources are GBK-encoded ALIENTEK drop-ins; they
# compile as-is (comments only). The LCD bus is configured register-level in
# lcd.c via the LL FMC driver, so the (broken) HAL_SRAM driver is not needed.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)