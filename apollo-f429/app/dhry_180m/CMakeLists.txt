# dhry_180m - apollo-f429 two-stage NAND boot stage-2 Dhrystone (SDRAM).
# Same Dhrystone 2.1 benchmark as bare/dhry_180m (identical sources + compiler
# flags), but linked entirely in SDRAM at 0xC0000000 and booted by the
# tool/boot loader - so both code AND data run from external SDRAM.
#
# Pico-style workflow:
#   bash build.sh            # == cmake -G Ninja .. && ninja
#   ninja flash              # -> NAND via the FMC-NAND algo, then reset
#   ninja flash-boot         # (helper) programs tool/boot into internal flash too

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

project(dhry_180m C ASM)

if(STM32_TOOLCHAIN STREQUAL "armclang")
    include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/armclang-postproject.cmake)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Stage-2 app: link entirely in SDRAM at 0xC0000000 and use the non-destructive
# SystemInit (the bootloader owns the clock tree).
set(STM32_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/app.ld" CACHE FILEPATH
    "stage-2 app linker script (SDRAM at 0xC0000000)" FORCE)
set(STM32F429_SYSTEM_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/system_app.c" CACHE FILEPATH
    "stage-2 app system init (non-destructive)" FORCE)

set(BENCH_OPT "-Ofast -ffp-contract=fast -funroll-loops" CACHE STRING
    "Optimization flags for stm32f429_apply_board (e.g. -Omax for armclang)")
set(BENCH_OPT_C "" CACHE STRING
    "Extra C-language-only optimization flags (e.g. armclang '-Omax'; not passed to ASM/link)")

# SDRAM is ~9x slower than the bare (internal-flash) run, so a full 2,000,000
# run Dhrystone takes ~50 s. The score (Dhrystones/s) is independent of the
# run count, so for quick runs pass e.g. -DDHRY_RUNS=200000. The recorded
# result below uses the default 2000000 (same final-value checks as bare).
set(DHRY_RUNS "2000000" CACHE STRING
    "Dhrystone runs (compile-time RUN_NUMBER override; default 2000000)")

add_executable(${PROJECT_NAME}.elf
    ../../bare/dhry_180m/src/main.c
    ../../bare/dhry_180m/src/utils.c
    ../../bare/dhry_180m/src/dhry_1.c
    ../../bare/dhry_180m/src/dhry_2.c
)

target_include_directories(${PROJECT_NAME}.elf PRIVATE
    ../../bare/dhry_180m/src
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f429_board.cmake)
stm32f429_apply_board(${PROJECT_NAME}.elf "${BENCH_OPT}")
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE
    NAND_APP=1
    RUN_NUMBER=${DHRY_RUNS}
)

if(BENCH_OPT_C)
    separate_arguments(BENCH_OPT_C_LIST NATIVE_COMMAND "${BENCH_OPT_C}")
    target_compile_options(${PROJECT_NAME}.elf PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:${BENCH_OPT_C_LIST}>")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/nand-flash-targets.cmake)