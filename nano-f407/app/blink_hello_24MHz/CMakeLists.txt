# LED blink + ADC internal-channel demo for the STM32F407VET6 "nano-f407" board,
# forced to run at 24 MHz instead of the default 168 MHz.
#
# Blinks the single LED (PB0, low-active) and periodically samples the three
# ADC1 internal channels (temperature IN16, VREFINT IN17, VBAT IN18) and prints
# them over USART1 (PA9/PA10) at 115200 baud (ST-Link VCP).
#
# This project brings its OWN clock config (src/clock_config.c -> 24 MHz). The
# board layer's SystemClock_Config() is weak, so this project's strong
# definition overrides it here only; every other nano-f407 project still runs
# at the default 168 MHz.
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

project(blink_hello_24MHz C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/clock_config.c
    src/adc_internal.c
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f407_board.cmake)
stm32f407_apply_board(${PROJECT_NAME}.elf "-O1")


include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)
