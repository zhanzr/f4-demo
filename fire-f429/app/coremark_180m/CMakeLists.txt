# SDRAM-backed CoreMark 1.0.1 for fire-f429.
cmake_minimum_required(VERSION 3.13)

if(POLICY CMP0123)
    cmake_policy(SET CMP0123 NEW)
endif()

# Toolchain selection. gcc (default) = GNU arm-none-eabi end-to-end;
# armclang = Keil Arm Compiler 6 C + GNU as/ld. Must be set before project().
set(STM32_TOOLCHAIN "gcc" CACHE STRING "C compiler: gcc (arm-none-eabi, default) or armclang (Keil AC6)")
set_property(CACHE STM32_TOOLCHAIN PROPERTY STRINGS gcc armclang)
option(STM32_LTO "Enable -flto with the GCC toolchain" OFF)

if(NOT CMAKE_TOOLCHAIN_FILE)
    if(STM32_TOOLCHAIN STREQUAL "armclang")
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/armclang-keil-toolchain.cmake")
    else()
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
    endif()
endif()

project(coremark_180m C ASM)

# armclang only: restore the GNU-style link rule after project() (CMake's
# ARMClang module installs an armlink-style rule that GNU ld cannot use).
if(STM32_TOOLCHAIN STREQUAL "armclang")
    include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/armclang-postproject.cmake)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(STM32_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../../board/stm32f429_sdram.ld")

# Default: -funroll-all-loops (from nano-f411 tuning, ~5% over -funroll-loops).
set(BENCH_OPT "-Ofast -ffp-contract=fast -funroll-all-loops" CACHE STRING
    "Optimization flags for stm32f429_apply_board (e.g. -Omax for armclang)")

# C-language-only optimization flags, applied to the C sources but NOT to the
# GNU-as startup/toolchain files or the link step. Required for armclang-only
# options that arm-none-eabi-gcc rejects (e.g. '-Omax'; pair with -fno-lto so
# GNU ld can link the result).
set(BENCH_OPT_C "" CACHE STRING
    "Extra C-language-only optimization flags (e.g. armclang '-Omax'; not passed to ASM/link)")
set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/../../bare/coremark_180m/coremark_1_0_1)

add_executable(${PROJECT_NAME}.elf
    ../../bare/coremark_180m/src/main.c
    ../../bare/coremark_180m/src/utils.c
    ../../bare/coremark_180m/src/core_portme.c
    ${CORE}/core_list_join.c
    ${CORE}/core_main.c
    ${CORE}/core_matrix.c
    ${CORE}/core_state.c
    ${CORE}/core_util.c
)

target_include_directories(${PROJECT_NAME}.elf PRIVATE
    ../../bare/coremark_180m/src
    ${CORE}
)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f429_board.cmake)
stm32f429_apply_board(${PROJECT_NAME}.elf "${BENCH_OPT}")
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE DATA_IN_ExtSDRAM)

if(BENCH_OPT_C)
    separate_arguments(BENCH_OPT_C_LIST NATIVE_COMMAND "${BENCH_OPT_C}")
    target_compile_options(${PROJECT_NAME}.elf PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:${BENCH_OPT_C_LIST}>")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)
