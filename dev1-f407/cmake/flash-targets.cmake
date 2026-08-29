# Shared flashing / SWV targets for the STM32F407VET6 custom board.
#
# The board can be programmed through either:
#   - Keil ULINK2  (enumerates as a CMSIS-DAP probe, VID:PID c251:2722)
#   - ST-Link V2   (STMicroelectronics ST-LINK, VID:PID 0483:3752)
#
# Targets:
#   ninja flash        - probe-rs download (default; auto-selects the probe)
#   ninja flash-ocd    - OpenOCD (interface/cmsis-dap + target/stm32f4x, SWD)
#   ninja swv          - monitor SWV/ITM printf via probe-rs `itm swo`.
#                        BLOCKS for the duration; Ctrl-C to stop.
#
# SWV parameters: TPIU clock (TRACECLKIN = HCLK) 168 MHz -> SWO baud 2 Mbaud.
# Duration 600000 ms (10 min). Adjust the BAUD/DURATION to taste.
#
# Probe selection (probe-rs --probe selector, VID:PID[:Serial]):
#   -DPROBE_SELECTOR=0483:3752:066FFF383337554E43133134   (ST-Link V2)
#   -DPROBE_SELECTOR=c251:2722:V0010M9E                   (Keil ULINK2)
#   -DPROBE_SELECTOR=auto   (default: let probe-rs pick the only connected probe)
#
# Overrides:
#   -DPROBE_RS=/path/to/probe-rs   -DOPENOCD=/path/to/openocd

set(PROBE_SELECTOR "auto" CACHE STRING
    "probe-rs --probe selector (VID:PID[:Serial]); 'auto' lets probe-rs pick the only connected probe")

find_program(PROBE_RS NAMES probe-rs probe-rs.exe
    HINTS "$ENV{USERPROFILE}/.cargo/bin" "$ENV{CARGO_HOME}/bin"
    DOC "probe-rs binary (preferred flasher / SWV monitor)")
find_program(OPENOCD NAMES openocd openocd.exe
    HINTS "C:/msys64/mingw64/bin"
    DOC "OpenOCD binary")

set(BIN_HEX "${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.hex")
set(OPENOCD_CFG "${CMAKE_CURRENT_LIST_DIR}/openocd_stm32f407ve.cfg")

# Build the probe-rs --probe argument. 'auto' omits it so probe-rs picks the
# only connected probe (works for both ULINK2 and ST-Link V2).
if(PROBE_SELECTOR STREQUAL "auto")
    set(PROBE_ARG "")
else()
    set(PROBE_ARG --probe "${PROBE_SELECTOR}")
endif()

# --- probe-rs ----------------------------------------------------------------
if(PROBE_RS)
    add_custom_target(flash
        COMMAND "${PROBE_RS}" download ${PROBE_ARG}
                    --chip STM32F407VE --protocol swd
                    --binary-format hex --verify --reset --non-interactive
                    --disable-progressbars "${BIN_HEX}"
        DEPENDS ${PROJECT_NAME}.elf
        COMMENT "Flashing ${PROJECT_NAME}.hex to STM32F407VET6 via probe-rs (SWD) ..."
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
        DEPENDS ${PROJECT_NAME}.elf
        COMMENT "Flashing ${PROJECT_NAME}.hex to STM32F407VET6 via OpenOCD (SWD) ..."
        USES_TERMINAL)
else()
    add_custom_target(flash-ocd
        COMMAND ${CMAKE_COMMAND} -E echo "OpenOCD not found. Install it (winget install xpack-dev-tools.openocd-xpack) or pass -DOPENOCD=/path/to/openocd.")
endif()

# --- SWV / ITM printf monitor ------------------------------------------------
if(PROBE_RS)
    add_custom_target(swv
        COMMAND "${PROBE_RS}" itm ${PROBE_ARG}
                    --chip STM32F407VE --protocol swd --non-interactive
                    swo 600000 168000000 2000000
        DEPENDS ${PROJECT_NAME}.elf
        COMMENT "Monitoring SWV/ITM printf @ 2 Mbaud (10 min). Ctrl-C to stop ..."
        USES_TERMINAL)
else()
    add_custom_target(swv
        COMMAND ${CMAKE_COMMAND} -E echo "probe-rs not found - SWV monitor unavailable.")
endif()
