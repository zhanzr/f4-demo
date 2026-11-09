# LED blink + ADC internal-channel demo for the Nucleo-F413ZH board
# (STM32F413ZHT6). Blinks the three user LEDs (LD1=PB0, LD2=PB7, LD3=PB14,
# all high-active) and periodically samples the ADC1 internal channels
# (temperature / VREFINT / VBAT) and prints them over USART3 (PD8/PD9) at
# 115200 baud (ST-Link VCP). Runs at 96 MHz from the 8 MHz ST-LINK MCO
# (HSE bypass).
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

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f413_board.cmake)
stm32f413_apply_board(${PROJECT_NAME}.elf "-O1")


include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)