# apollo-f429 `app`: two-stage firmware that boots the STM32F429IG from
# internal flash and runs the main app from SDRAM (0xC0000000), which shadows
# the on-board NAND (MT29F4G08ABADA).
#
#   boot.elf  - stage 1 bootloader in internal flash. Initializes clocks / UART /
#               SDRAM / NAND, copies the app image from NAND offset 0 to SDRAM
#               0xC0000000, validates it and jumps.
#   app.elf   - stage 2 app linked at 0xC0000000 (SDRAM, bank 1). blink_hello
#               migrated: prints &main + &bss to prove the remap. Uses the
#               non-destructive SystemInit (bootloader owns the clock tree).
#
# The stage-2 app is written into the NAND with the probe-rs custom FMC-NAND
# flash algorithm (see app/algo + flash-nand target).
#
# Targets:
#   ninja            - builds boot.elf + app.elf (+ .hex/.bin)
#   ninja flash-boot - probe-rs -> boot.hex into internal flash (STM32F429IG)
#   ninja flash-app  - build + run the NAND flash algo, write app.hex to NAND
#   ninja flash      - alias for flash-boot
#
# NOTE: the NAND is not memory-mapped (no XIP like h723's OSPI) - the
# bootloader *copies* its pages to SDRAM and executes from there.

cmake_minimum_required(VERSION 3.13)

if(POLICY CMP0123)
    cmake_policy(SET CMP0123 NEW)
endif()

set(STM32_TOOLCHAIN "gcc" CACHE STRING "C compiler: gcc (arm-none-eabi, default) or armclang (Keil AC6)")
set_property(CACHE STM32_TOOLCHAIN PROPERTY STRINGS gcc armclang)
option(STM32_LTO "Enable -flto with the GCC toolchain" OFF)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/arm-none-eabi-toolchain.cmake")
endif()

project(apollo_app C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/stm32f429_board.cmake)

# ---------------------------------------------------------------------------
# Stage 1: bootloader (internal flash)
# ---------------------------------------------------------------------------
set(STM32_LINKER_SCRIPT "${BOARD_DIR}/stm32f429igt6.ld" CACHE FILEPATH
    "bootloader linker script (internal flash)" FORCE)
set(STM32F429_SYSTEM_SOURCE "${BOARD_DIR}/system_stm32f4xx.c" CACHE FILEPATH
    "bootloader system init (stock, owns the clocks)" FORCE)

add_executable(boot.elf
    boot/src/main.c
    boot/src/nand.c
)
stm32f429_apply_board(boot.elf "-O1")
target_include_directories(boot.elf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/boot/src)
# FMC NAND HAL driver (HAL_NAND_Init / HAL_NAND_MspInit callbacks in nand.c).
target_sources(boot.elf PRIVATE ${STM32F4_HAL_SRC}/stm32f4xx_hal_nand.c)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/boot.hex
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:boot.elf> ${CMAKE_CURRENT_BINARY_DIR}/boot.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:boot.elf>
    DEPENDS boot.elf
    COMMENT "objcopy -> boot.hex (Intel HEX for internal flash)")
add_custom_target(boot_hex ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/boot.hex)

# ---------------------------------------------------------------------------
# Stage 2: SDRAM app (0xC0000000). Override linker + system source:
#   - the stock SystemInit() would reset RCC and confuse the bootloader-owned
#     clock tree, so use the non-destructive system_app.c;
#   - app.ld places everything in SDRAM 0xC0000000.
# ---------------------------------------------------------------------------
set(STM32_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/app/app.ld" CACHE FILEPATH
    "SDRAM app linker script (everything at 0xC0000000)" FORCE)
set(STM32F429_SYSTEM_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/app/src/system_app.c" CACHE FILEPATH
    "SDRAM app system init (non-destructive)" FORCE)

add_executable(app.elf
    app/src/main.c
    app/src/system_app.c
)
stm32f429_apply_board(app.elf "-O1")
target_compile_definitions(app.elf PRIVATE NAND_APP=1)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/app.hex ${CMAKE_CURRENT_BINARY_DIR}/app.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:app.elf> ${CMAKE_CURRENT_BINARY_DIR}/app.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:app.elf> ${CMAKE_CURRENT_BINARY_DIR}/app.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:app.elf>
    DEPENDS app.elf
    COMMENT "objcopy -> app.hex/.bin (Intel HEX @ 0xC0000000)")
add_custom_target(app_hex ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/app.hex)

# ---------------------------------------------------------------------------
# Flashing
# ---------------------------------------------------------------------------
# The ULINK2 (CMSIS-DAP / SWD) is the standard probe on this board.
set(ULINK2_PROBE "c251:2722:V0010M9E" CACHE STRING
    "probe-rs --probe selector (VID:PID[:Serial]) of the Keil ULINK2")

find_program(PROBE_RS NAMES probe-rs probe-rs.exe
    HINTS "$ENV{USERPROFILE}/.cargo/bin" "$ENV{CARGO_HOME}/bin")
find_program(PYTHON NAMES python python3 py
    HINTS "$ENV{LOCALAPPDATA}/Programs/Python"
          "$ENV{USERPROFILE}/AppData/Local/Python")

set(ALGO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/algo")
set(ALGO_SRC "${ALGO_DIR}/flash_nand_fmc.c")
set(ALGO_YAML "${ALGO_DIR}/target_nand_fmc.yaml")

if(PROBE_RS)
    # boot.hex -> internal flash.
    add_custom_target(flash-boot
        COMMAND "${PROBE_RS}" download --probe "${ULINK2_PROBE}"
                    --chip STM32F429IG --protocol swd
                    --connect-under-reset
                    --binary-format hex --verify --reset --non-interactive
                    --disable-progressbars ${CMAKE_CURRENT_BINARY_DIR}/boot.hex
        DEPENDS boot_hex
        COMMENT "Flashing boot.hex to STM32F429IGT6 via probe-rs (ULINK2, SWD) ..."
        USES_TERMINAL)
    add_custom_target(flash DEPENDS flash-boot)

    # app.hex -> NAND via the register-level FMC-NAND flash algorithm.
    # The YAML is (re)generated when missing or older than the source.
    if(PYTHON)
        add_custom_command(
            OUTPUT "${ALGO_YAML}"
            COMMAND "${PYTHON}" build_algo.py flash_nand_fmc.c 0x800
            WORKING_DIRECTORY "${ALGO_DIR}"
            DEPENDS "${ALGO_SRC}"
            COMMENT "Generating NAND flash algorithm ${ALGO_YAML} ...")
        add_custom_target(flash-app
            COMMAND "${PROBE_RS}" download --probe "${ULINK2_PROBE}"
                        --chip-description-path "${ALGO_YAML}"
                        --chip "STM32F429IG-NAND-nand_fmc" --protocol swd
                        --connect-under-reset
                        --binary-format hex --non-interactive --disable-progressbars
                        ${CMAKE_CURRENT_BINARY_DIR}/app.hex
            COMMAND "${PROBE_RS}" reset --probe "${ULINK2_PROBE}"
                        --chip STM32F429IG --protocol swd
                        --connect-under-reset --non-interactive
            DEPENDS app_hex "${ALGO_YAML}"
            COMMENT "Flashing app.hex to the NAND via probe-rs FMC-NAND algorithm ..."
            USES_TERMINAL)
    else()
        add_custom_target(flash-app
            COMMAND ${CMAKE_COMMAND} -E echo "python not found - cannot build the NAND flash algorithm.")
    endif()
else()
    add_custom_target(flash-boot
        COMMAND ${CMAKE_COMMAND} -E echo "probe-rs not found (-DPROBE_RS=/path/to/probe-rs)")
    add_custom_target(flash DEPENDS flash-boot)
    add_custom_target(flash-app
        COMMAND ${CMAKE_COMMAND} -E echo "probe-rs not found (-DPROBE_RS=/path/to/probe-rs)")
endif()