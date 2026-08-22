# LED blink + ADC internal-channel demo for the fire-f429 board (STM32F429IGT6).
# Bare-metal (built-in flash + SRAM only). Blinks LED_R/G/B (PH10/11/12) and
# LED_1 (PD12) one by one, prints the 180 MHz clock and the ADC1 internal
# channels (VREFINT / temperature / VBAT) over USART1 (PA9/PA10) at 115200
# through the CMSIS-DAP VCP.
#
# Pico-style workflow:
#   mkdir -p build && cd build
#   cmake -G Ninja ..
#   ninja           # builds .elf / .hex / .bin
#   ninja flash     # programs the board (probe-rs, Fire CMSIS-DAP SWD)

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

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f429_board.cmake)
stm32f429_apply_board(${PROJECT_NAME}.elf "-O1")

# Generate .hex/.bin and copy them next to the sources (where the flasher looks).
add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex ${PROJECT_NAME}.elf ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary ${PROJECT_NAME}.elf ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROJECT_NAME}.hex ${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.hex
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROJECT_NAME}.bin ${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} ${PROJECT_NAME}.elf
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "objcopy -> .hex/.bin, size")

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)