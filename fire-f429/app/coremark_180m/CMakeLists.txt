# SDRAM-backed CoreMark 1.0.1 for fire-f429.
cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
endif()

project(coremark_180m C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(STM32_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../../board/stm32f429_sdram.ld")
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
stm32f429_apply_board(${PROJECT_NAME}.elf "-Ofast -ffp-contract=fast -funroll-loops")
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE DATA_IN_ExtSDRAM)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/flash-targets.cmake)
