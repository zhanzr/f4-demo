# ST7735S 1.44" 128x128 TFT demo for the STM32F407VET6 "nano-f407" board.
# Drives the panel over soft (bit-banged) SPI on PB13/PB15, CS=PB12, DC=PC6,
# RST=PB1, BL=PE9 (TIM1 PWM at 8%) and cycles test patterns. Runs at 24 MHz
# from the 8 MHz HSE (PLLN=192/PLLP=8).
#
# Requires the TIM HAL module for the backlight PWM: stm32f4xx_hal_tim.c and
# stm32f4xx_hal_tim_ex.c are appended here because the board layer compiles
# only the modules it needs by default. SPI is bit-banged, so no HAL SPI.
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

project(st7735s_md144_128_128 C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(STM32F4_HAL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../../drivers/STM32F4xx_HAL_Driver/Src")

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/st7735s.c
    ${STM32F4_HAL_SRC}/stm32f4xx_hal_tim.c
    ${STM32F4_HAL_SRC}/stm32f4xx_hal_tim_ex.c
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f407_board.cmake)
stm32f407_apply_board(${PROJECT_NAME}.elf "-O1")

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
