# CoreMark 1.0.1 benchmark for the dev1-f407 board (custom STM32F407VET6) @ 168 MHz.
# Default compiler is GNU arm-none-eabi-gcc at the most aggressive optimization
# (-Ofast -ffp-contract=fast -funroll-loops) + hard-float; Keil Arm Compiler 6
# (armclang) is available as an option. Linked with GNU ld/newlib via the
# existing linker script.
#
# Pico-style workflow:
#   mkdir -p build && cd build
#   cmake -G Ninja ..                    # defaults to gcc (arm-none-eabi)
#   cmake -G Ninja -DSTM32_TOOLCHAIN=armclang ..   # Keil AC6 (option); -DSTM32_LTO=ON enables gcc LTO
#   ninja           # builds .elf / .hex / .bin
#   ninja flash     # programs the board (probe-rs, ULINK2 CMSIS-DAP / SWD)
#   ninja swv       # stream the SWV printf output (Ctrl-C to stop)

cmake_minimum_required(VERSION 3.13)

# We pass the cpu/arch flags ourselves (-mcpu=...) and link with GNU ld, so
# keep CMake's ARMClang module from auto-adding --cpu/--march flags.
if(POLICY CMP0123)
    cmake_policy(SET CMP0123 NEW)
endif()

# Toolchain selection. gcc (default) = GNU arm-none-eabi end-to-end;
# armclang = Keil Arm Compiler 6 C + GNU as/ld. CMAKE_TOOLCHAIN_FILE is cached,
# so use a separate build dir per toolchain (build/ for gcc, build-armclang/
# for armclang).
set(STM32_TOOLCHAIN "gcc" CACHE STRING "C compiler: gcc (arm-none-eabi, default) or armclang (Keil AC6)")
set_property(CACHE STM32_TOOLCHAIN PROPERTY STRINGS gcc armclang)

# Link-time optimization. GCC only: armclang -flto/-Omax emits LLVM bitcode
# (.llvm.lto) that needs armlink + ARMCLIB and cannot be consumed by GNU ld.
option(STM32_LTO "Enable -flto with the GCC toolchain" OFF)

if(NOT CMAKE_TOOLCHAIN_FILE)
    if(STM32_TOOLCHAIN STREQUAL "gcc")
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
    else()
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/armclang-keil-toolchain.cmake")
    endif()
endif()

project(coremark_168m C ASM)

# armclang only: restore the GNU-style link rule after project() (CMake's
# ARMClang module installs an armlink-style rule that GNU ld cannot use).
if(STM32_TOOLCHAIN STREQUAL "armclang")
    include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/armclang-postproject.cmake)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/coremark_1_0_1)

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/utils.c
    src/core_portme.c
    ${CORE}/core_list_join.c
    ${CORE}/core_main.c
    ${CORE}/core_matrix.c
    ${CORE}/core_state.c
    ${CORE}/core_util.c
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f407_board.cmake)
stm32f407_apply_board(${PROJECT_NAME}.elf "-Ofast -ffp-contract=fast -funroll-loops")

target_include_directories(${PROJECT_NAME}.elf PRIVATE
    src
    ${CORE}
)


include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)
