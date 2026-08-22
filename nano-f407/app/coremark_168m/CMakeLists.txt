# CoreMark 1.0.1 benchmark for the STM32F407VET6 "nano-f407" board @ 168 MHz.
# Compiled with Keil Arm Compiler 6 (armclang) at the most aggressive
# optimization (-Ofast -ffp-contract=fast -funroll-loops) + hard-float;
# linked with GNU ld/newlib via the existing linker script.
#
# Pico-style workflow:
#   mkdir -p build && cd build
#   cmake -G Ninja ..                    # defaults to armclang (Keil AC6)
#   cmake -G Ninja -DSTM32_TOOLCHAIN=gcc -DSTM32_LTO=ON ..   # GNU + LTO
#   ninja           # builds .elf / .hex / .bin
#   ninja flash     # programs the board (probe-rs, ULINK2 CMSIS-DAP / SWD)
#   ninja swv       # stream the SWV printf output (Ctrl-C to stop)

cmake_minimum_required(VERSION 3.13)

# We pass the cpu/arch flags ourselves (-mcpu=...) and link with GNU ld, so
# keep CMake's ARMClang module from auto-adding --cpu/--march flags.
if(POLICY CMP0123)
    cmake_policy(SET CMP0123 NEW)
endif()

# Toolchain selection. armclang (default) = Keil Arm Compiler 6 C + GNU as/ld;
# gcc = GNU arm-none-eabi end-to-end. CMAKE_TOOLCHAIN_FILE is cached, so use a
# separate build dir per toolchain (build/ for armclang, build-gcc/ for gcc).
set(STM32_TOOLCHAIN "armclang" CACHE STRING "C compiler: armclang (Keil AC6) or gcc")
set_property(CACHE STM32_TOOLCHAIN PROPERTY STRINGS armclang gcc)

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

# Generate .hex/.bin and copy them next to the sources (where the flasher looks).
add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex ${PROJECT_NAME}.elf ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary ${PROJECT_NAME}.elf ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROJECT_NAME}.hex ${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.hex
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROJECT_NAME}.bin ${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} ${PROJECT_NAME}.elf
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "objcopy -> .hex/.bin, size")

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)
