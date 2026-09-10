# blink_hello - apollo-f429 two-stage NAND boot stage-2 app (SDRAM).
# Migrated from bare/blink_hello: LED blink + ADC internal-channel report, plus
# &main / &bss prints to prove the NAND->SDRAM remap. Linked entirely at
# 0xC0000000 (SDRAM bank 1); the stack stays in built-in SRAM. The bootloader
# (tool/boot) copies it from NAND offset 0 and jumps, so it MUST NOT reown the
# clock tree - see system_app.c + the NAND_APP board.c guard.
#
# Pico-style workflow:
#   bash build.sh            # == cmake -G Ninja .. && ninja
#   ninja flash              # writes app.hex -> the NAND via the probe-rs
#                            # FMC-NAND flash algorithm (tool/nand_flash_algo)
#   ninja flash-boot         # (optional) programs the stage-1 bootloader +
#                            # app into internal flash / NAND in one go
#
# Prerequisite: tool/boot must be in internal flash first (one-time per board).

cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
endif()

project(blink_hello C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Stage-2 app: link entirely in SDRAM at 0xC0000000 and use the non-destructive
# SystemInit (the bootloader owns the clock tree).
set(STM32_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/app.ld" CACHE FILEPATH
    "stage-2 app linker script (SDRAM at 0xC0000000)" FORCE)
set(STM32F429_SYSTEM_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/system_app.c" CACHE FILEPATH
    "stage-2 app system init (non-destructive)" FORCE)

add_executable(${PROJECT_NAME}.elf
    src/main.c
    src/system_app.c
    src/adc_internal.c
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f429_board.cmake)
stm32f429_apply_board(${PROJECT_NAME}.elf "-O1")
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE NAND_APP=1)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/nand-flash-targets.cmake)