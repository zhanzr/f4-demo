# Shared flashing / SWV targets for the fire-f429 board (STM32F429IGT6),
# programmed through a **Keil ULINK2** (works with this board; the on-board
# Fire CMSIS-DAP does not deliver SWD responses on Windows).
#
# Targets:
#   ninja flash        - probe-rs download over ULINK2 (SWD)
#
# Overrides:
#   -DPROBE_RS=/path/to/probe-rs
#   -DULINK2_PROBE=c251:2722:V0010M9E   (probe-rs --probe selector for the ULINK2)

set(ULINK2_PROBE "c251:2722:V0010M9E" CACHE STRING
    "probe-rs --probe selector (VID:PID[:Serial]) of the Keil ULINK2")

find_program(PROBE_RS NAMES probe-rs probe-rs.exe
    HINTS "$ENV{USERPROFILE}/.cargo/bin" "$ENV{CARGO_HOME}/bin"
    DOC "probe-rs binary (preferred flasher)")

set(BIN_HEX "${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.hex")

# --- probe-rs ----------------------------------------------------------------
if(PROBE_RS)
    add_custom_target(flash
        COMMAND "${PROBE_RS}" download --probe "${ULINK2_PROBE}"
                    --chip STM32F429IG --protocol swd
                    --binary-format hex --verify --reset --non-interactive
                    --disable-progressbars "${BIN_HEX}"
        DEPENDS ${PROJECT_NAME}.elf
        COMMENT "Flashing ${PROJECT_NAME}.hex to STM32F429IGT6 via probe-rs (ULINK2, SWD) ..."
        USES_TERMINAL)
else()
    add_custom_target(flash
        COMMAND ${CMAKE_COMMAND} -E echo "probe-rs not found. Install it (cargo install probe-rs-tools) or pass -DPROBE_RS=/path/to/probe-rs.")
endif()