# recorder_test - apollo-f429 stage-2 app: record 10 s on the WM8978 mic via
# SAI1 into SDRAM, then play it back on the WM8978 speaker (LED0 on=recording,
# off=playing). No SD card / no FATFS: raw 16-bit stereo PCM is kept in SDRAM
# (the volatile memory the two-stage boot runs the app from).
#
# Ported from the vendored ALIENTEK Apollo 实验46 录音机实验 (wm8978 + sai),
# simplified to an in-memory loop. Uses the shared stage-2 SDRAM firmware
# (tool/boot) and non-destructive SystemInit.
#
# Pico-style workflow:
#   bash build.sh            # == cmake -G Ninja .. && ninja
#   ninja flash              # -> NAND via the FMC-NAND algo, then reset
#   ninja flash-boot         # (helper) programs tool/boot into internal flash too

cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/arm-none-eabi-toolchain.cmake")
endif()

project(recorder_test C ASM)

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
    src/wm8978.c
    src/sai.c
    src/myiic.c
    src/delay.c
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f429_board.cmake)
stm32f429_apply_board(${PROJECT_NAME}.elf "-O1")
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE NAND_APP=1)

target_include_directories(${PROJECT_NAME}.elf PRIVATE src)

# SAI driver + PLLI2S clocking (HAL_SAI / HAL_RCCEx). The board HAL conf already
# enables HAL_RCC_EX_MODULE_ENABLED + HAL_SAI_MODULE_ENABLED.
target_sources(${PROJECT_NAME}.elf PRIVATE
    ${STM32F4_HAL_SRC}/stm32f4xx_hal_sai.c
    ${STM32F4_HAL_SRC}/stm32f4xx_hal_sai_ex.c
    ${STM32F4_HAL_SRC}/stm32f4xx_hal_rcc_ex.c
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/nand-flash-targets.cmake)