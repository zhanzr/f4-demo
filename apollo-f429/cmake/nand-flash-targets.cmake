# Shared `ninja flash` targets for apollo-f429 two-stage stage-2 apps.
#
# Unlike flash-targets.cmake (which programs INTERNAL flash), this writes the
# app .hex into the on-board NAND (MT29F4G08ABADA) at 0xC0000000 using the
# probe-rs FMC-NAND flash algorithm in ../tool/nand_flash_algo/.
#
# The algorithm YAML is auto-regenerated when missing or older than
# flash_nand_fmc.c, so no manual build_algo.py step is needed.
#
# Targets:
#   ninja flash      - build the .hex, (re)build the algo if needed, write the
#                      app to NAND, reset the board so the stage-1 bootloader
#                      loads and runs it from SDRAM
#   ninja flash-boot - program the stage-1 bootloader into INTERNAL flash
#   ninja bin        - raw .bin (for the bootloader / ext tools)
#
# Prerequisite (one-time per board): tool/boot must be in internal flash.
#
# Overrides:
#   -DPROBE_RS=/path/to/probe-rs   -DULINK2_PROBE=<selector>
#   -DPYTHON=/path/to/python
set(ULINK2_PROBE "c251:2722:V0010M9E" CACHE STRING
    "probe-rs --probe selector (VID:PID[:Serial]) of the Keil ULINK2")

set(NAND_ALGO_DIR "${CMAKE_CURRENT_LIST_DIR}/../tool/nand_flash_algo")
set(NAND_ALGO_SRC "${NAND_ALGO_DIR}/flash_nand_fmc.c")
set(NAND_ALGO_YAML "${NAND_ALGO_DIR}/target_nand_fmc.yaml")
set(BIN_HEX "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.hex")
set(BIN_BIN "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.bin")
set(BOOT_HEX "${CMAKE_CURRENT_LIST_DIR}/../tool/boot/build/boot.hex")

add_custom_command(OUTPUT ${BIN_HEX} ${BIN_BIN}
    COMMAND ${CMAKE_OBJCOPY} -O ihex   "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.elf" "${BIN_HEX}"
    COMMAND ${CMAKE_OBJCOPY} -O binary "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.elf" "${BIN_BIN}"
    COMMAND ${CMAKE_SIZE} "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.elf"
    DEPENDS ${PROJECT_NAME}.elf
    VERBATIM
    COMMENT "objcopy -> .hex/.bin (in build/), size")
add_custom_target(hex ALL DEPENDS ${BIN_HEX} ${BIN_BIN})

find_program(PROBE_RS NAMES probe-rs probe-rs.exe
    HINTS "$ENV{USERPROFILE}/.cargo/bin" "$ENV{CARGO_HOME}/bin"
    DOC "probe-rs binary (preferred flasher)")

find_program(PYTHON NAMES python python3 py
    HINTS "$ENV{LOCALAPPDATA}/Programs/Python"
          "$ENV{USERPROFILE}/AppData/Local/Python"
    DOC "python interpreter (to build the flash algorithm YAML)")

if(PROBE_RS AND PYTHON)
    # (Re)generate the algorithm YAML only when missing or older than the source.
    add_custom_command(
        OUTPUT "${NAND_ALGO_YAML}"
        COMMAND "${PYTHON}" build_algo.py flash_nand_fmc.c 0x800
        WORKING_DIRECTORY "${NAND_ALGO_DIR}"
        DEPENDS "${NAND_ALGO_SRC}"
        COMMENT "Generating NAND flash algorithm ${NAND_ALGO_YAML} ...")

    add_custom_target(flash
        COMMAND "${PROBE_RS}" download --probe "${ULINK2_PROBE}"
                    --chip-description-path "${NAND_ALGO_YAML}"
                    --chip "STM32F429IG-NAND-nand_fmc" --protocol swd
                    --connect-under-reset
                    --binary-format hex --non-interactive --disable-progressbars
                    "${BIN_HEX}"
        COMMAND "${PROBE_RS}" reset --probe "${ULINK2_PROBE}"
                    --chip STM32F429IG --protocol swd
                    --connect-under-reset --non-interactive
        COMMAND ${CMAKE_COMMAND} -E echo ""
        COMMAND ${CMAKE_COMMAND} -E echo "NAND flashed; board reset - bootloader loads the new app."
        DEPENDS hex "${NAND_ALGO_YAML}"
        COMMENT "Flashing ${PROJECT_NAME}.hex to the NAND (probe-rs FMC-NAND algorithm)"
        USES_TERMINAL)

    # Belt-and-braces helper: (re)program the stage-1 bootloader into internal
    # flash, then NAND-flash the app (full two-stage refresh in one command).
    add_custom_target(flash-boot
        COMMAND ${CMAKE_COMMAND} -E echo "Programming stage-1 bootloader (${BOOT_HEX}) ..."
        COMMAND "${PROBE_RS}" download --probe "${ULINK2_PROBE}"
                    --chip STM32F429IG --protocol swd --connect-under-reset
                    --binary-format hex --verify --reset --non-interactive
                    --disable-progressbars "${BOOT_HEX}"
        COMMAND ${CMAKE_COMMAND} -E echo "Bootloader done. Now flashing the app to NAND ..."
        COMMAND "${PROBE_RS}" download --probe "${ULINK2_PROBE}"
                    --chip-description-path "${NAND_ALGO_YAML}"
                    --chip "STM32F429IG-NAND-nand_fmc" --protocol swd
                    --connect-under-reset
                    --binary-format hex --non-interactive --disable-progressbars
                    "${BIN_HEX}"
        COMMAND "${PROBE_RS}" reset --probe "${ULINK2_PROBE}"
                    --chip STM32F429IG --protocol swd
                    --connect-under-reset --non-interactive
        COMMAND ${CMAKE_COMMAND} -E echo ""
        COMMAND ${CMAKE_COMMAND} -E echo "Two-stage refresh complete."
        DEPENDS hex "${NAND_ALGO_YAML}"
        COMMENT "Flash stage-1 bootloader (internal) + stage-2 app (NAND) in one go"
        USES_TERMINAL)
else()
    add_custom_target(flash
        COMMAND ${CMAKE_COMMAND} -E echo
                "probe-rs and/or python not found. Set -DPROBE_RS=/path/to/probe-rs and -DPYTHON=/path/to/python.")
    add_custom_target(flash-boot
        COMMAND ${CMAKE_COMMAND} -E echo
                "probe-rs and/or python not found. Set -DPROBE_RS=/path/to/probe-rs and -DPYTHON=/path/to/python.")
endif()