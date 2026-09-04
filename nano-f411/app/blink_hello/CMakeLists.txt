# LED blink + ADC internal-channel demo for the STM32F411CEU6 "nano-f411" board.
# Blinks the single LED (PC13, low-active) and periodically samples the ADC1
# internal channels (temperature / VREFINT / VBAT on the shared IN18 input) and
# prints them over USART1 (PA9/PA10) at 115200 baud (ST-Link VCP). Runs at
# 100 MHz from the 25 MHz HSE crystal.
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

project(blink_hello C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/adc_internal.c
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f411_board.cmake)
stm32f411_apply_board(${PROJECT_NAME}.elf "-O1")


include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)