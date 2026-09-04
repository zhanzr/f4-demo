# Shared STM32F407VET6 board layer for the "nano-f407" board projects.
# The board support (clock init to 168 MHz from the 8 MHz HSE crystal, LED GPIO,
# SWV/ITM printf, newlib stubs, startup, linker script) plus the STM32F4 HAL
# driver sources are attached to a target with stm32f407_apply_board().
#
# Usage (from a project CMakeLists.txt, after add_executable()):
#   include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f407_board.cmake)
#   stm32f407_apply_board(${PROJECT_NAME}.elf "-Ofast")
#
# Requires the project to enable ASM (project(X C ASM)).

# Vendored HAL/CMSIS (repo root `drivers/`). To use the full STM32Cube_FW_F4
# package instead, point this at its root (contains Drivers/).
#   e.g. -DSTM32F4_HAL_ROOT="C:/Users/user1/STM32Cube/Repository/STM32Cube_FW_F4_V1.28.3"
set(STM32F4_HAL_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../drivers" CACHE PATH
    "Root of the STM32F4 HAL+ CMSIS (vendored `drivers/` or a full STM32Cube_FW_F4 package)")

set(BOARD_DIR ${CMAKE_CURRENT_LIST_DIR}/../board)
set(STM32F4_HAL_INC ${STM32F4_HAL_ROOT}/STM32F4xx_HAL_Driver/Inc)
set(STM32F4_HAL_SRC ${STM32F4_HAL_ROOT}/STM32F4xx_HAL_Driver/Src)
set(STM32F4_CMSIS_DEV ${STM32F4_HAL_ROOT}/CMSIS/Device/ST/STM32F4xx/Include)
set(STM32F4_CMSIS_CORE ${STM32F4_HAL_ROOT}/CMSIS/Include)

function(stm32f407_apply_board TGT OPT)
    separate_arguments(OPT_LIST NATIVE_COMMAND "${OPT}")

    # GCC-only warning switches; keep clang-based toolchains clean.
    if(STM32_ARMCLANG OR STM32_STARM_CLANG)
        set(_WARN_FLAGS -Wall -Wno-unused-command-line-argument)
    else()
        set(_WARN_FLAGS
            -Wall
            -Wno-unused-but-set-variable -Wno-unused-function
            -Wno-unused-variable -Wno-unused-parameter -Wno-maybe-uninitialized)
    endif()

    target_sources(${TGT} PRIVATE
        ${BOARD_DIR}/board.c
        ${BOARD_DIR}/stm32f4xx_hal_msp.c
        ${BOARD_DIR}/stm32f4xx_it.c
        ${BOARD_DIR}/swv_printf.c
        ${BOARD_DIR}/uart_printf.c
        ${BOARD_DIR}/syscalls.c
        ${BOARD_DIR}/startup_stm32f407xx.s
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

    target_compile_definitions(${TGT} PRIVATE STM32F407xx USE_HAL_DRIVER)

    target_compile_options(${TGT} PRIVATE
        -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
        ${OPT_LIST} -g
        -ffunction-sections -fdata-sections ${_WARN_FLAGS}
    )

    # starm-clang links with LLD by default; use -Xlinker for linker flags.
    if(STM32_STARM_CLANG)
        set(_STARM_SYSROOT "${STARM_ROOT}/lib/clang-runtimes/newlib")
        set(_STARM_LIBDIR "${_STARM_SYSROOT}/arm-none-eabi/armv7m_hard_fpv4_sp_d16_exn_rtti_unaligned_size/lib")
        set(_LDFLAGS "-nostartfiles")
        set(_LDFLAGS "${_LDFLAGS} -Xlinker -T -Xlinker ${BOARD_DIR}/stm32f407vet6.ld")
        set(_LDFLAGS "${_LDFLAGS} -Xlinker --gc-sections")
        set(_LDFLAGS "${_LDFLAGS} -Xlinker -Map=${PROJECT_NAME}.map")
        set(_LDFLAGS "${_LDFLAGS} -L${_STARM_LIBDIR}")
        set(_LDFLAGS "${_LDFLAGS} -Xlinker --start-group")
        set(_LDFLAGS "${_LDFLAGS} ${_STARM_LIBDIR}/libclang_rt.builtins.a -lc -lm")
        set(_LDFLAGS "${_LDFLAGS} -Xlinker --end-group")
    else()
        set(_LDFLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 ${OPT}")
        set(_LDFLAGS "${_LDFLAGS} -Wl,--gc-sections -nostartfiles")
        set(_LDFLAGS "${_LDFLAGS} -Wl,-Map=${PROJECT_NAME}.map")
        set(_LDFLAGS "${_LDFLAGS} -T ${BOARD_DIR}/stm32f407vet6.ld")
        set(_LDFLAGS "${_LDFLAGS} -lc -lm")
    endif()

    set_target_properties(${TGT} PROPERTIES
        LINK_FLAGS "${_LDFLAGS}"
    )

    # newlib/libgcc's thumb/v7e-m+fp/hard multilib objects are built with
    # -fshort-enums and lack .note.GNU-stack, so a GNU ld link spews ~60
    # benign warnings. Silence them (sizes match the ARM EABI defaults our
    # objects use, so this is noise, not an ABI error). LLD (starm-clang)
    # does not support these GNU-ld-only switches, so skip them there.
    if(NOT STM32_STARM_CLANG)
        set_property(TARGET ${TGT} APPEND_STRING PROPERTY
            LINK_FLAGS " -Wl,--no-enum-size-warning -Wl,--no-wchar-size-warning -Wl,--no-warn-execstack")
    endif()

    # Link-time optimization (GCC and starm-clang).
    # GCC: -flto via GNU ld's lto plugin.
    # starm-clang: -flto=full via LLD's native LTO (bitcode consumed directly).
    # Keil armclang: LTO impossible — emits LLVM bitcode that GNU ld cannot use.
    if(STM32_LTO AND STM32_STARM_CLANG)
        # Full LLVM LTO: compile to bitcode (-flto=full), link with LLD's LTO.
        # Keep -ffat-lto-objects so syscalls.c-style -fno-lto sources still
        # produce relocatable objects. The aggressive pass thresholds come from
        # ST's OmaxLTO.cfg (inlined/unrolled via -Wl,-plugin-opt at link time,
        # see below). C++-only devirt flags are omitted (pure-C benchmarks).
        target_compile_options(${TGT} PRIVATE
            -flto=full -ffat-lto-objects
        )
        set_property(TARGET ${TGT} APPEND_STRING PROPERTY
            LINK_FLAGS " -flto=full")
        # Pass aggressive LTO plugin opts (from ST's OmaxLTO.cfg).
        set_property(TARGET ${TGT} APPEND_STRING PROPERTY
            LINK_FLAGS " -Xlinker -plugin-opt=-extra-LTO-loop-unroll=true")
        set_property(TARGET ${TGT} APPEND_STRING PROPERTY
            LINK_FLAGS " -Xlinker -plugin-opt=-inline-threshold=500")
        set_property(TARGET ${TGT} APPEND_STRING PROPERTY
            LINK_FLAGS " -Xlinker -plugin-opt=-unroll-threshold=450")
        set_property(TARGET ${TGT} APPEND_STRING PROPERTY
            LINK_FLAGS " -Xlinker -plugin-opt=-unroll-partial-threshold=450")
        set_property(TARGET ${TGT} APPEND_STRING PROPERTY
            LINK_FLAGS " -Xlinker -plugin-opt=-unroll-max-iteration-count-to-analyze=20")
        set_property(TARGET ${TGT} APPEND_STRING PROPERTY
            LINK_FLAGS " -Xlinker -plugin-opt=-lsr-complexity-limit=1073741823")
        set_property(TARGET ${TGT} APPEND_STRING PROPERTY
            LINK_FLAGS " -Xlinker -plugin-opt=-force-attribute=main:norecurse")
        set_property(TARGET ${TGT} APPEND_STRING PROPERTY
            LINK_FLAGS " -Xlinker -plugin-opt=-enable-dfa-jump-thread")
        set_property(TARGET ${TGT} APPEND_STRING PROPERTY
            LINK_FLAGS " -Xlinker -plugin-opt=-enable-loop-flatten")
        set_property(TARGET ${TGT} APPEND_STRING PROPERTY
            LINK_FLAGS " -Xlinker -plugin-opt=-enable-unroll-and-jam")
        set_property(TARGET ${TGT} APPEND_STRING PROPERTY
            LINK_FLAGS " -Xlinker -plugin-opt=-enable-inline-memcpy-ld-st")
        set_property(TARGET ${TGT} APPEND_STRING PROPERTY
            LINK_FLAGS " -Xlinker -plugin-opt=-enable-loop-versioning-licm")
        # syscalls.c: tiny retarget layer, compile without LTO to avoid
        # newlib syscall-stub resolution issues (same rationale as GCC).
        set_source_files_properties(${BOARD_DIR}/syscalls.c PROPERTIES
            COMPILE_OPTIONS "-fno-lto")
    elseif(STM32_LTO AND NOT STM32_ARMCLANG AND NOT STM32_STARM_CLANG)
        target_compile_options(${TGT} PRIVATE -flto)
        set_property(TARGET ${TGT} APPEND_STRING PROPERTY LINK_FLAGS " -flto")
        set_source_files_properties(${BOARD_DIR}/syscalls.c PROPERTIES
            COMPILE_OPTIONS "-fno-lto")
    endif()
endfunction()
