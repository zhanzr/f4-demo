# ram_test — RAM/CCM execution probes (nano-f407, STM32F407VET6)

Minimal test that links small functions into RAM memory via a project linker
script and copy-in's them from flash at startup, to isolate whether the CPU
can execute code from a given memory region. The default build places the
resident functions in **SRAM2** (0x2001C000); a second linker script targets
**CCM** (0x10000000) instead.

## Files

- `src/main.c` — harness in flash; calls the resident functions and reports.
- `src/ram_func.c`: loop-based function (heavier).
- `src/ccm_function.c`: trivial 2-instruction `a + b` (barest probe).
- `stm32f407ram_test.ld` — default: resident functions → SRAM2.
- `stm32f407ram_test_ccm.ld` — alternative: resident functions → CCM.

## Test status (measured on hardware, nano-f407 — healthy chip)

| Resident function location | Result |
| -------------------------- | ------ |
| **SRAM2** (0x2001C000)     | **Works** — `ccm_function(7,5)=12`, `ram_hello` matches reference |
| **CCM** (0x10000000)       | **Does not work** — hard fault (BFSR.IBUSERR) on first CCM instruction fetch |

The exact same source runs correctly from SRAM2 but faults the instant it
fetches an instruction from CCM. CCM **data** access works and CCM already
holds the correct code bytes (verified by `ccm_probe`); only the instruction
**fetch** fails. This is reproduced on the healthy nano-f407 and the dev1-f407,
so it is a chip-level STM32F4 trait (CCM is D-bus/data-only), not a board
defect. See the board `README.md` "RAM / CCM code-execution test status".

## Build

Default (SRAM2):

```bash
mkdir -p build && cd build
cmake -G Ninja ..
ninja
ninja flash          # probe-rs / ST-Link (SWD)
```

To test CCM instead, point `BOARD_LINKER_SCRIPT` at
`stm32f407ram_test_ccm.ld` (in `CMakeLists.txt`) and rebuild in a fresh dir.

Console: board USART1 / ST-Link VCP (115200 8-N-1); on this setup reachable via
the CH340 USB-serial port (auto-detect, e.g. COM4).
