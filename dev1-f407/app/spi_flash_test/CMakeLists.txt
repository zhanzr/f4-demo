# W25Q64 SPI flash erase/program/read test for dev1-f407.
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
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f407_board.cmake)
stm32f407_apply_board(${PROJECT_NAME}.elf "-O2")

add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex ${PROJECT_NAME}.elf ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary ${PROJECT_NAME}.elf ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROJECT_NAME}.hex ${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.hex
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROJECT_NAME}.bin ${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} ${PROJECT_NAME}.elf
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "objcopy -> .hex/.bin, size")
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)