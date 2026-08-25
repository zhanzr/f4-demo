# ov5640_to_lcd_clone: stable OV5640 -> LTDC camera app on the fire-f429.
# Single consolidated camera project (the other ov5640_to_lcd_* variants were
# merged here). Built on the vendored STM32F4 HAL + shared board layer; the
# only app-local sources are the camera driver (bsp_ov5640), the SCCB wrapper
# (bsp_i2c) and the vendor auto-focus (ov5640_AF).
#
# Toolchain: GNU arm-none-eabi-gcc by default; Keil AC6 armclang optional.
#
#   cmake -G Ninja -B build .            # gcc (default)
#   ninja -C build                       # -> ov5640_to_lcd_clone.hex/.bin
#   ninja -C build flash
#   cmake -G Ninja -B build-ac6 -DSTM32_TOOLCHAIN=armclang .
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

project(ov5640_to_lcd_clone C ASM)

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
    src/bsp_ov5640.c
    src/bsp_i2c.c
    src/ov5640_AF.c
)
target_include_directories(${PROJECT_NAME}.elf PRIVATE ../../board src)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f429_board.cmake)
stm32f429_apply_board(${PROJECT_NAME}.elf "-O3")

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
