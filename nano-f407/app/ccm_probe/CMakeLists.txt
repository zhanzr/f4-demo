# ccm_probe -- focused CCM instruction-fetch isolation for the nano-f407.
# Places only ccm_func.c into CCM (.ram_code @ 0x10000000) and copy-in's it from
# flash at startup; the harness (main) stays in flash. main proves CCM data
# access works and that the code bytes are correctly present at the fetch
# address, then calls the CCM function to expose the fetch fault.
#
# Pico-style workflow (gcc default; armclang / starm-clang for alternate CCs):
#   mkdir -p build && cd build
#   cmake -G Ninja ..                    # gcc
#   cmake -G Ninja -DSTM32_TOOLCHAIN=armclang ..
#   cmake -G Ninja -DSTM32_TOOLCHAIN=starm-clang ..
#   ninja
#   ninja flash

cmake_minimum_required(VERSION 3.13)
if(POLICY CMP0123)
    cmake_policy(SET CMP0123 NEW)
endif()

set(STM32_TOOLCHAIN "gcc" CACHE STRING "C compiler: gcc (default), armclang (Keil AC6), or starm-clang (ST LLVM)")
set_property(CACHE STM32_TOOLCHAIN PROPERTY STRINGS gcc armclang starm-clang)
option(STM32_LTO "Enable LTO (gcc or starm-clang only)" OFF)

if(NOT CMAKE_TOOLCHAIN_FILE)
    if(STM32_TOOLCHAIN STREQUAL "gcc")
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
    elseif(STM32_TOOLCHAIN STREQUAL "armclang")
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/armclang-keil-toolchain.cmake")
    else()
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/starm-clang-toolchain.cmake")
    endif()
endif()

project(ccm_probe C ASM)

if(STM32_TOOLCHAIN STREQUAL "armclang")
    include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/armclang-postproject.cmake)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(BOARD_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/stm32f407ccm_probe.ld)

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/ccm_func.c
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f407_board.cmake)
stm32f407_apply_board(${PROJECT_NAME}.elf "-O1")

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)
