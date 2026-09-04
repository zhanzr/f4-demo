# board_hello - fire-f429 board self-test (SDRAM-remapped app).
# Reads the on-board sensors: ADC1 internal channels (VREFINT/temp/VBAT),
# PA4 GL5516 light sensor, DHT11 (PE2), MPU6050 (I2C1 PB6/PB7).
cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
endif()

project(board_hello C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(STM32_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../../board/stm32f429_sdram.ld")

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/adc_internal.c
    src/dht11.c
    src/mpu6050.c
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f429_board.cmake)
stm32f429_apply_board(${PROJECT_NAME}.elf "-O1")
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE DATA_IN_ExtSDRAM)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)
