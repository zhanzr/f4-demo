# ov7670_dcmi_probe: does the OV7670 actually emit pixels on the DCMI bus?
#
# Step 2 of the OV7670 bring-up. SCCB is already proven (minimal pins,
# bit-bang; PID 0x76 VER 0x73). Now: with the sensor configured for QVGA
# RGB565 + COLOR BAR, arm DCMI in CONTINUOUS mode with a CIRCULAR DMA ring
# (the proven jpeg_test pattern) and watch:
#   - does the DMA NDTR advance?  -> pixels ARE flowing (clock+sync OK)
#   - full-ring scan: color bar produces 0xF800/0x07E0/0x001F word patterns
#     (red/green/blue), which prove the 8-bit RGB565 bus is wired correctly.
#
# Console-only (USART1 115200), NO LCD. Build/flash like the other apps:
#   cmake -G Ninja -B build . && ninja -C build && ninja -C build flash
cmake_minimum_required(VERSION 3.13)

set(STM32_TOOLCHAIN "gcc" CACHE STRING "C compiler")
set_property(CACHE STM32_TOOLCHAIN PROPERTY STRINGS gcc armclang)

if(NOT CMAKE_TOOLCHAIN_FILE)
    if(STM32_TOOLCHAIN STREQUAL "armclang")
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/armclang-keil-toolchain.cmake")
    else()
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
    endif()
endif()

project(ov7670_dcmi_probe C ASM)

if(STM32_TOOLCHAIN STREQUAL "armclang")
    include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/armclang-postproject.cmake)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(STM32_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../../board/stm32f429igt6.ld")

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/sccb_bitbang.c
    src/ov7670_regs.c
)
target_include_directories(${PROJECT_NAME}.elf PRIVATE ../../board src)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f429_board.cmake)
stm32f429_apply_board(${PROJECT_NAME}.elf "-O2")

add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex ${PROJECT_NAME}.elf ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary ${PROJECT_NAME}.elf ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROJECT_NAME}.hex ${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.hex
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROJECT_NAME}.bin ${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} ${PROJECT_NAME}.elf
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "objcopy -> .hex/.bin, size")
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)