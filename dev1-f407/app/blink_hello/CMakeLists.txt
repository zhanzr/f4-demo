# LED blink + ADC internal-channel demo for the dev1-f407 board (custom
# STM32F407VET6). Blinks LED1/2/3 (PE13/14/15, low-active) and periodically
# samples the three ADC1 internal channels (temperature IN16, VREFINT IN17,
# VBAT IN18) and prints them over SWV/ITM (SWO). Runs the CPU at 168 MHz
# from the 25 MHz HSE crystal.
#
# Pico-style workflow:
#   mkdir -p build && cd build
#   cmake -G Ninja ..
#   ninja           # builds .elf / .hex / .bin
#   ninja flash     # programs the board (probe-rs, ULINK2 CMSIS-DAP / SWD)
#   ninja swv       # stream the SWV printf output (Ctrl-C to stop)

cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
endif()

project(blink_hello C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/adc_internal.c
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f407_board.cmake)
stm32f407_apply_board(${PROJECT_NAME}.elf "-O1")


include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)
