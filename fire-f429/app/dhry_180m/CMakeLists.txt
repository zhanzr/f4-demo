# SDRAM-backed Dhrystone 2.1 for fire-f429.
cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
endif()

project(dhry_180m C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(STM32_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../../board/stm32f429_sdram.ld")

add_executable(${PROJECT_NAME}.elf
    ../../bare/dhry_180m/src/main.c
    ../../bare/dhry_180m/src/utils.c
    ../../bare/dhry_180m/src/dhry_1.c
    ../../bare/dhry_180m/src/dhry_2.c
)

target_include_directories(${PROJECT_NAME}.elf PRIVATE ../../bare/dhry_180m/src)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f429_board.cmake)
stm32f429_apply_board(${PROJECT_NAME}.elf "-Ofast -ffp-contract=fast -funroll-loops")
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE DATA_IN_ExtSDRAM)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)
