# LTDC 5-inch touch LCD test for fire-f429 (SDRAM app).
cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
endif()

project(ltdc_touch_test C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(STM32_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../../board/stm32f429_sdram.ld")

add_executable(${PROJECT_NAME}.elf
    src/main.c
    ../../board/board_ltdc.c
)
target_include_directories(${PROJECT_NAME}.elf PRIVATE ../../board)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f429_board.cmake)
stm32f429_apply_board(${PROJECT_NAME}.elf "-O2")
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE DATA_IN_ExtSDRAM)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)