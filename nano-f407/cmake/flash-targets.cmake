# Shared flashing / SWV targets for the STM32F407VET6 "nano-f407" board, programmed
# through an ST-Link (SWD). The ST-Link's virtual COM port (VCP) is the console.
#
# Targets:
#   ninja flash        - probe-rs download (default; ST-Link SWD, auto-detected)
#   ninja flash-ocd    - OpenOCD (interface/stlink + target/stm32f4x, SWD)
#
# How to view a probe's selector (VID:PID:Serial): run `probe-rs list` with
# the probe plugged in (e.g. `0483:374b:xxxx...` for an ST-Link V2).
#
# Overrides:
#   -DPROBE_RS=/path/to/probe-rs   -DOPENOCD=/path/to/openocd
#   -DDEBUG_PROBE=<probe selector>   (probe-rs --probe selector; empty = auto-detect)

set(DEBUG_PROBE "" CACHE STRING
    "probe-rs --probe selector (VID:PID[:Serial], e.g. 0483:374b:xxxx...); empty = auto-detect")

# When DEBUG_PROBE is empty, omit --probe so probe-rs auto-detects the probe.
if(DEBUG_PROBE)
    set(PROBE_ARGS --probe "${DEBUG_PROBE}")
else()
    set(PROBE_ARGS)
endif()

find_program(PROBE_RS NAMES probe-rs probe-rs.exe
    HINTS "$ENV{USERPROFILE}/.cargo/bin" "$ENV{CARGO_HOME}/bin"
    DOC "probe-rs binary (preferred flasher / SWV monitor)")
find_program(OPENOCD NAMES openocd openocd.exe
    HINTS "C:/msys64/mingw64/bin"
    DOC "OpenOCD binary")

# All build products (.elf/.hex/.bin/.map) live in the project `build/` folder
# (git-ignored); nothing is copied next to the sources. The .hex/.bin are
# generated here as ninja-visible OUTPUTs of the linked elf, and the flash
# targets depend on the .hex so it is always (re)generated before flashing.
set(BIN_ELF "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.elf")
set(BIN_HEX "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.hex")
set(BIN_BIN "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.bin")
set(OPENOCD_CFG "${CMAKE_CURRENT_LIST_DIR}/openocd_stm32f407ve.cfg")

add_custom_command(OUTPUT ${BIN_HEX} ${BIN_BIN}
    COMMAND ${CMAKE_OBJCOPY} -O ihex   "${BIN_ELF}" "${BIN_HEX}"
    COMMAND ${CMAKE_OBJCOPY} -O binary "${BIN_ELF}" "${BIN_BIN}"
    COMMAND ${CMAKE_SIZE} "${BIN_ELF}"
    DEPENDS ${PROJECT_NAME}.elf
    VERBATIM
    COMMENT "objcopy -> .hex/.bin (in build/), size")

# Part of the default `all` build, so plain `ninja` also emits .hex/.bin.
add_custom_target(${PROJECT_NAME}_hex ALL DEPENDS ${BIN_HEX} ${BIN_BIN})

# --- probe-rs ----------------------------------------------------------------
if(PROBE_RS)
    add_custom_target(flash
        COMMAND "${PROBE_RS}" download ${PROBE_ARGS}
                    --chip STM32F407VE --protocol swd
                    --binary-format hex --verify --reset --non-interactive
                    --disable-progressbars "${BIN_HEX}"
        DEPENDS ${BIN_HEX}
        COMMENT "Flashing ${PROJECT_NAME}.hex to STM32F407VET6 via probe-rs (ST-Link, SWD) ..."
        USES_TERMINAL)
else()
    add_custom_target(flash
        COMMAND ${CMAKE_COMMAND} -E echo "probe-rs not found. Install it (cargo install probe-rs-tools) or pass -DPROBE_RS=/path/to/probe-rs.")
endif()

# --- OpenOCD -----------------------------------------------------------------
if(OPENOCD)
    add_custom_target(flash-ocd
        COMMAND "${OPENOCD}" -f "${OPENOCD_CFG}"
                    -c "program ${BIN_HEX} verify reset exit"
        DEPENDS ${BIN_HEX}
        COMMENT "Flashing ${PROJECT_NAME}.hex to STM32F407VET6 via OpenOCD (ST-Link, SWD) ..."
        USES_TERMINAL)
else()
    add_custom_target(flash-ocd
        COMMAND ${CMAKE_COMMAND} -E echo "OpenOCD not found. Install it (winget install xpack-dev-tools.openocd-xpack) or pass -DOPENOCD=/path/to/openocd.")
endif()
