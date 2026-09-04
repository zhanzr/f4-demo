# Shared flashing targets for the fire-f429 board (STM32F429IGT6), programmed
# through a Keil ULINK2 (CMSIS-DAP, SWD).
#
# Targets (measured on this board; ~45 KB image, erased baseline):
#   ninja flash         - OpenOCD  (default; fastest, ~3 s, verify+reset)
#   ninja flash-probe   - probe-rs (~11 s)
#   ninja flash-pyocd   - pyOCD    (~9 s; ~7 s overhead when content is identical)
#
# Overrides:
#   -DOPENOCD=/path/to/openocd   -DPROBE_RS=/path/to/probe-rs
#   -DULINK2_PROBE=<selector>    (probe-rs --probe selector, default c251:2722:V0010M9E)

set(ULINK2_PROBE "c251:2722:V0010M9E" CACHE STRING
    "probe-rs --probe selector (VID:PID[:Serial]) of the Keil ULINK2")
set(ULINK2_UID "V0010M9E" CACHE STRING
    "pyOCD unique-id of the Keil ULINK2 (the probe-rs selector's serial part)")

find_program(OPENOCD NAMES openocd openocd.exe
    HINTS "C:/msys64/mingw64/bin"
    DOC "OpenOCD binary")
find_program(PROBE_RS NAMES probe-rs probe-rs.exe
    HINTS "$ENV{USERPROFILE}/.cargo/bin" "$ENV{CARGO_HOME}/bin"
    DOC "probe-rs binary")

# All build products (.elf/.hex/.bin/.map) live in the project `build/` folder
# (git-ignored); nothing is copied next to the sources. The .hex/.bin are
# generated here as ninja-visible OUTPUTs of the linked elf, and the flash
# targets depend on the .hex so it is always (re)generated before flashing.
set(BIN_ELF "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.elf")
set(BIN_HEX "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.hex")
set(BIN_BIN "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.bin")
set(OPENOCD_IF  "C:/msys64/mingw64/share/openocd/scripts/interface/cmsis-dap.cfg")
set(OPENOCD_TGT "C:/msys64/mingw64/share/openocd/scripts/target/stm32f4x.cfg")
# note: OpenOCD's "program" needs a forward-slash path (backslashes are escapes).
string(REPLACE "\\" "/" BIN_HEX_OCD "${BIN_HEX}")

add_custom_command(OUTPUT ${BIN_HEX} ${BIN_BIN}
    COMMAND ${CMAKE_OBJCOPY} -O ihex   "${BIN_ELF}" "${BIN_HEX}"
    COMMAND ${CMAKE_OBJCOPY} -O binary "${BIN_ELF}" "${BIN_BIN}"
    COMMAND ${CMAKE_SIZE} "${BIN_ELF}"
    DEPENDS ${PROJECT_NAME}.elf
    VERBATIM
    COMMENT "objcopy -> .hex/.bin (in build/), size")

# Part of the default `all` build, so plain `ninja` also emits .hex/.bin.
add_custom_target(${PROJECT_NAME}_hex ALL DEPENDS ${BIN_HEX} ${BIN_BIN})

# --- OpenOCD (default) -------------------------------------------------------
# Fastest + most reliable for the ULINK2 on this board.
if(OPENOCD)
    add_custom_target(flash
        COMMAND "${OPENOCD}" -f "${OPENOCD_IF}" -f "${OPENOCD_TGT}"
                    -c "adapter speed 4000" -c "transport select swd"
                    -c "program ${BIN_HEX_OCD} verify reset exit"
        DEPENDS ${BIN_HEX}
        COMMENT "Flashing ${PROJECT_NAME}.hex to STM32F429IGT6 via OpenOCD (ULINK2, SWD) ..."
        USES_TERMINAL)
else()
    add_custom_target(flash
        COMMAND ${CMAKE_COMMAND} -E echo "OpenOCD not found. Install it (winget install xpack-dev-tools.openocd-xpack) or pass -DOPENOCD=/path/to/openocd.")
endif()

# --- probe-rs -----------------------------------------------------------------
if(PROBE_RS)
    add_custom_target(flash-probe
        COMMAND "${PROBE_RS}" download --probe "${ULINK2_PROBE}"
                    --chip STM32F429IG --protocol swd
                    --binary-format hex --verify --reset --non-interactive
                    --disable-progressbars "${BIN_HEX}"
        DEPENDS ${BIN_HEX}
        COMMENT "Flashing ${PROJECT_NAME}.hex to STM32F429IGT6 via probe-rs (ULINK2, SWD) ..."
        USES_TERMINAL)
else()
    add_custom_target(flash-probe
        COMMAND ${CMAKE_COMMAND} -E echo "probe-rs not found. Install it (cargo install probe-rs-tools) or pass -DPROBE_RS=/path/to/probe-rs.")
endif()

# --- pyOCD --------------------------------------------------------------------
find_program(PYTHON NAMES python python.exe DOC "Python (for the pyOCD flasher)")
if(PYTHON)
    add_custom_target(flash-pyocd
        COMMAND "${PYTHON}" -u -m pyocd flash "${BIN_HEX}"
                    -t stm32f429xi -u "${ULINK2_UID}" --no-config
        DEPENDS ${BIN_HEX}
        COMMENT "Flashing ${PROJECT_NAME}.hex to STM32F429IGT6 via pyOCD (ULINK2, SWD) ..."
        USES_TERMINAL)
else()
    add_custom_target(flash-pyocd
        COMMAND ${CMAKE_COMMAND} -E echo "Python/pyOCD not found - pyOCD flasher unavailable.")
endif()