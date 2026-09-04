# ov7670_to_lcd: OV7670 (no FIFO) -> LTDC camera app on the fire-f429.
#
# The ov7670 user module has NO FIFO chip, so the sensor is interfaced
# directly through the STM32 DCMI parallel interface (8-bit RGB565), exactly
# like the OV5640 in app/ov5640_to_lcd_clone.
#
# Unlike the OV5640 module (active 24 MHz crystal -> XCLK is an input), this
# OV7670 module has NO clock source at all, so the MCU drives the sensor's
# XCLK: PA8 = MCO1 outputs the 25 MHz HSE crystal (OV7670 accepts 10-48 MHz).
#
# Only app-local sources: the OV7670 driver (bsp_ov7670) + the SCCB wrapper
# (bsp_i2c). No copied vendor drivers - everything else is the vendored HAL
# and the shared board layer.
#
# Toolchain: GNU arm-none-eabi-gcc by default; Keil AC6 armclang optional.
#
#   cmake -G Ninja -B build .            # gcc (default)
#   ninja -C build                       # -> ov7670_to_lcd.hex/.bin
#   ninja -C build flash
cmake_minimum_required(VERSION 3.13)

set(STM32_TOOLCHAIN "gcc" CACHE STRING "C compiler: gcc (default) or armclang (Keil AC6)")
set_property(CACHE STM32_TOOLCHAIN PROPERTY STRINGS gcc armclang)

if(NOT CMAKE_TOOLCHAIN_FILE)
    if(STM32_TOOLCHAIN STREQUAL "armclang")
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/armclang-keil-toolchain.cmake")
    else()
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
    endif()
endif()

project(ov7670_to_lcd C ASM)

if(STM32_TOOLCHAIN STREQUAL "armclang")
    include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/armclang-postproject.cmake)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# .data/.bss in internal SRAM; only the framebuffers live in SDRAM
# (fixed 0xD0000000 region), initialized by Board_SDRAM_EarlyInit.
set(STM32_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../../board/stm32f429igt6.ld")

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/lcd_camera.c
    src/bsp_ov7670.c
    src/bsp_i2c.c
)
target_include_directories(${PROJECT_NAME}.elf PRIVATE ../../board src)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f429_board.cmake)
stm32f429_apply_board(${PROJECT_NAME}.elf "-O3")

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)