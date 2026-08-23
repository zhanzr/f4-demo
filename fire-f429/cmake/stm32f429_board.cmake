# Shared STM32F429IGT6 board layer for the fire-f429 board projects.
# The board support (clock init to 180 MHz from the 25 MHz HSE crystal, LED GPIO,
# SWV/ITM printf, newlib stubs, startup, linker script) plus the STM32F4 HAL
# driver sources are attached to a target with stm32f429_apply_board().
#
# Usage (from a project CMakeLists.txt, after add_executable()):
#   include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f429_board.cmake)
#   stm32f429_apply_board(${PROJECT_NAME}.elf "-Ofast")
#
# Requires the project to enable ASM (project(X C ASM)).

# Vendored HAL/CMSIS (repo root `drivers/`). To use the full STM32Cube_FW_F4
# package instead, point this at its root (contains Drivers/).
#   e.g. -DSTM32F4_HAL_ROOT="C:/Users/user1/STM32Cube/Repository/STM32Cube_FW_F4_V1.28.3"
set(STM32F4_HAL_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../drivers" CACHE PATH
    "Root of the STM32F4 HAL + CMSIS (vendored `drivers/` or a full STM32Cube_FW_F4 package)")

set(BOARD_DIR ${CMAKE_CURRENT_LIST_DIR}/../board)
set(STM32F4_HAL_INC ${STM32F4_HAL_ROOT}/STM32F4xx_HAL_Driver/Inc)
set(STM32F4_HAL_SRC ${STM32F4_HAL_ROOT}/STM32F4xx_HAL_Driver/Src)
set(STM32F4_CMSIS_DEV ${STM32F4_HAL_ROOT}/CMSIS/Device/ST/STM32F4xx/Include)
set(STM32F4_CMSIS_CORE ${STM32F4_HAL_ROOT}/CMSIS/Include)

if(NOT STM32_LINKER_SCRIPT)
    set(STM32_LINKER_SCRIPT ${BOARD_DIR}/stm32f429igt6.ld)
endif()

function(stm32f429_apply_board TGT OPT)
    separate_arguments(OPT_LIST NATIVE_COMMAND "${OPT}")

    # GCC-only warning switches; keep clang (armclang) clean.
    if(STM32_ARMCLANG)
        set(_WARN_FLAGS -Wall)
    else()
        set(_WARN_FLAGS
            -Wall
            -Wno-unused-but-set-variable -Wno-unused-function
            -Wno-unused-variable -Wno-unused-parameter -Wno-maybe-uninitialized)
    endif()

    target_sources(${TGT} PRIVATE
        ${BOARD_DIR}/board.c
        ${BOARD_DIR}/board_sdram.c
        ${BOARD_DIR}/stm32f4xx_hal_msp.c
        ${BOARD_DIR}/stm32f4xx_it.c
        ${BOARD_DIR}/swv_printf.c
        ${BOARD_DIR}/uart_printf.c
        ${BOARD_DIR}/syscalls.c
        ${BOARD_DIR}/startup_stm32f429xx.s
        ${BOARD_DIR}/system_stm32f4xx.c
        ${STM32F4_HAL_SRC}/stm32f4xx_hal.c
        ${STM32F4_HAL_SRC}/stm32f4xx_hal_adc.c
        ${STM32F4_HAL_SRC}/stm32f4xx_hal_adc_ex.c
        ${STM32F4_HAL_SRC}/stm32f4xx_hal_cortex.c
        ${STM32F4_HAL_SRC}/stm32f4xx_hal_dma.c
        ${STM32F4_HAL_SRC}/stm32f4xx_hal_gpio.c
        ${STM32F4_HAL_SRC}/stm32f4xx_hal_flash.c
        ${STM32F4_HAL_SRC}/stm32f4xx_hal_flash_ex.c
        ${STM32F4_HAL_SRC}/stm32f4xx_hal_pwr.c
        ${STM32F4_HAL_SRC}/stm32f4xx_hal_pwr_ex.c
        ${STM32F4_HAL_SRC}/stm32f4xx_hal_rcc.c
        ${STM32F4_HAL_SRC}/stm32f4xx_hal_rcc_ex.c
        ${STM32F4_HAL_SRC}/stm32f4xx_hal_uart.c
        ${STM32F4_HAL_SRC}/stm32f4xx_hal_sdram.c
        ${STM32F4_HAL_SRC}/stm32f4xx_hal_spi.c
        ${STM32F4_HAL_SRC}/stm32f4xx_hal_i2c.c
        ${STM32F4_HAL_SRC}/stm32f4xx_ll_fmc.c
    )

    target_include_directories(${TGT} PRIVATE
        ${BOARD_DIR}
        ${STM32F4_HAL_INC}
        ${STM32F4_HAL_INC}/Legacy
        ${STM32F4_CMSIS_DEV}
        ${STM32F4_CMSIS_CORE}
    )

    # armclang has no bundled libc headers: point it at the GNU newlib include
    # dir so <stdio.h>/<string.h>/... resolve to the same newlib we link.
    if(STM32_ARMCLANG)
        target_include_directories(${TGT} SYSTEM PRIVATE
            "${ARM_GCC_ROOT}/arm-none-eabi/include"
        )
    endif()

    target_compile_definitions(${TGT} PRIVATE STM32F429xx USE_HAL_DRIVER)

    target_compile_options(${TGT} PRIVATE
        -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
        ${OPT_LIST} -g
        -ffunction-sections -fdata-sections ${_WARN_FLAGS}
    )

    set_target_properties(${TGT} PROPERTIES
        LINK_FLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 ${OPT} -Wl,--gc-sections -nostartfiles -Wl,-Map=${PROJECT_NAME}.map -T ${STM32_LINKER_SCRIPT} -lc -lm"
    )

    # newlib/libgcc's thumb/v7e-m+fp multilib objects are built with
    # -fshort-enums and lack .note.GNU-stack, so a normal link spews ~60
    # benign warnings. Silence them (the sizes match the ARM EABI defaults
    # our objects use, so this is noise, not an ABI error):
    set_property(TARGET ${TGT} APPEND_STRING PROPERTY
        LINK_FLAGS " -Wl,--no-enum-size-warning -Wl,--no-wchar-size-warning -Wl,--no-warn-execstack")

    # Link-time optimization (GCC only). armclang -flto emits LLVM bitcode
    # (.llvm.lto) that GNU ld cannot consume, so STM32_LTO is ignored there.
    if(STM32_LTO AND NOT STM32_ARMCLANG)
        target_compile_options(${TGT} PRIVATE -flto)
        set_property(TARGET ${TGT} APPEND_STRING PROPERTY LINK_FLAGS " -flto")
        set_source_files_properties(${BOARD_DIR}/syscalls.c PROPERTIES
            COMPILE_OPTIONS "-fno-lto")
    endif()
endfunction()