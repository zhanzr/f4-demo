# jpeg_test: probe the fire-f429 OV5640 sensor for real JPEG output.
#
# Motivation: the module is silk-screened "FD5640 500W-V1 1" (a suspected
# third-party clone). The esp32-camera issue tracker (#307, #368) shows fake
# OV5640s failing to emit a valid JPEG stream (cam_hal: NO-SOI). Instead of
# guessing, this app empirically tests the sensor:
#
#   1. Reads PID (0x300A/B) and the OmniVision manufacturer ID (0x300C/D) -
#      a genuine OV5640 reads MIDH=0x7F MIDL=0xA2. Anything odd is a strong
#      clone signal.
#   2. Arms the DCMI (snapshot) and captures raw bytes on the 8-bit parallel
#      bus, scanning every captured buffer for JPEG Start-Of-Image (FF D8),
#      End-Of-Image (FF D9) and quantization-table (FF DB) markers, plus the
#      JPEG IIIF/VFIFO control registers (0x4713/0x4608).
#   3. Tries the sensor's JPEG output modes:
#        - mode A: vendor RGB565 base + JPEG enable (0x3821 bit5, 0x4713 mode,
#          0x4300=0, 0x501f=0x30, 0x3002=0, 0x3006=0xff) at QVGA
#        - mode B: same at VGA
#        - mode C: same at WVGA (800x480)
#        - mode D: esp32-camera's sensor_fmt_jpeg register list + QVGA
#   4. Prints a verdict: JPEG supported / JPEG broken (NO-SOI) / RGB-only.
#
# It does NOT touch the display (no LTDC layer). Console output only, via the
# board's USART1 print (same as the other fire-f429 apps). Each test phase
# prints the DCMI RISR flags and the DMA byte/word counters so a capture that
# got zero bytes (sensor not even clocking data) is distinguishable from one
# that captured YUV/RGB data but no JPEG markers.
#
# Notes:
# - Reuses the proven driver code: bsp_i2c.c (SCCB) + the DCMI/DMA init from
#   bsp_ov5640.c, compiled with the shared board layer (board.c HAL_Init,
#   180 MHz, USART1). The config register tables (RGB565_Init / QVGA / VGA /
#   WVGA) live in bsp_ov5640.c and are re-used verbatim (they are ordinary
#   {addr,val} tables we already know how to drive).
# - The snapshot DMA is re-armed per capture with the same pattern as the
#   proven lcd app (OV5640_DMA_Config). DCMI error flags are cleared before
#   the HAL IRQ handler (newer HAL aborts the DMA on ERR/OVR otherwise).
#
# Build/flash (from this dir):
#   cmake -G Ninja -B build .            # gcc (default)
#   ninja -C build                       # -> jpeg_test.hex/.bin
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

project(jpeg_test C ASM)

if(STM32_TOOLCHAIN STREQUAL "armclang")
    include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/armclang-postproject.cmake)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# .data/.bss in internal SRAM; no SDRAM used in this console-only probe.
set(STM32_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../../board/stm32f429igt6.ld")

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/probe_jpeg.c
    src/bsp_i2c.c
    src/bsp_ov5640.c
)
target_include_directories(${PROJECT_NAME}.elf PRIVATE ../../board src)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f429_board.cmake)
stm32f429_apply_board(${PROJECT_NAME}.elf "-O2")

# bsp_i2c.c uses the I2C analog-filter API - the shared board layer does not
# compile this HAL source, so add it for this app.
target_sources(${PROJECT_NAME}.elf PRIVATE
    ${STM32F4_HAL_SRC}/stm32f4xx_hal_i2c_ex.c)

add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex ${PROJECT_NAME}.elf ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary ${PROJECT_NAME}.elf ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROJECT_NAME}.hex ${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.hex
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROJECT_NAME}.bin ${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} ${PROJECT_NAME}.elf
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "objcopy -> .hex/.bin, size")
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)