# coremark_180m - apollo-f429 two-stage NAND boot stage-2 CoreMark (SDRAM).
# Same CoreMark 1.0.1 benchmark as bare/coremark_180m (identical sources +
# compiler flags), but linked entirely in SDRAM at 0xC0000000 and booted by
# the tool/boot loader - so both code AND data run from external SDRAM.
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

project(coremark_180m C ASM)

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

# Default: -funroll-all-loops (from nano-f411 tuning, ~5% over -funroll-loops).
set(BENCH_OPT "-Ofast -ffp-contract=fast -funroll-all-loops" CACHE STRING
    "Optimization flags for stm32f429_apply_board (e.g. -Omax for armclang)")
set(BENCH_OPT_C "" CACHE STRING
    "Extra C-language-only optimization flags (e.g. armclang '-Omax'; not passed to ASM/link)")

# SDRAM is ~10x slower than the bare (internal-flash) run, so a full 10000
# iteration CoreMark takes ~3.6 min. The score (iterations/s) is independent
# of the iteration count, so for quick runs pass e.g. -DCORE_ITERATIONS=2000.
# The recorded result below uses the default 10000 (matches the bare CRC).
set(CORE_ITERATIONS "10000" CACHE STRING
    "CoreMark iterations (compile-time ITERATIONS override; default 10000)")

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
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE
    NAND_APP=1
    ITERATIONS=${CORE_ITERATIONS}
)

if(BENCH_OPT_C)
    separate_arguments(BENCH_OPT_C_LIST NATIVE_COMMAND "${BENCH_OPT_C}")
    target_compile_options(${PROJECT_NAME}.elf PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:${BENCH_OPT_C_LIST}>")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/nand-flash-targets.cmake)