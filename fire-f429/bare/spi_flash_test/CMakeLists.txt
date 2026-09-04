# W25Q128FVSG SPI flash erase/program/read test for fire-f429.
cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
endif()

project(spi_flash_test C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}.elf
    src/main.c
    ../../board/spi_flash.c
)
target_include_directories(${PROJECT_NAME}.elf PRIVATE ../../board)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f429_board.cmake)
stm32f429_apply_board(${PROJECT_NAME}.elf "-O2")

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)
