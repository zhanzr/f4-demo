# ram_test — RAM/CCM execution probes (dev1-f407, STM32F407VET6)

Minimal test that links small functions into RAM memory via a project linker
script and copy-in's them from flash at startup, to isolate whether the CPU
can execute code from a given memory region. The default build places the
resident functions in **SRAM2** (0x2001C000); a second linker script targets
**CCM** (0x10000000) instead.

## Files

- `src/main.c` — harness in flash; calls the resident functions and reports.
- `src/ram_hello.c`: loop-based function (heavier).
- `src/ccm_function.c`: trivial 2-instruction `a + b` (barest probe).
- `stm32f407ram_test.ld` — default: resident functions → SRAM2.
- `stm32f407ram_test_ccm.ld` — alternative: resident functions → CCM.

## Test status (measured on hardware, dev1-f407)

| Resident function location | Result |
| -------------------------- | ------ |
| **SRAM2** (0x2001C000)     | **Works** — `ccm_function(7,5)=12`, `ram_hello` matches reference |
| **CCM** (0x10000000)       | **Does not work** — hard fault (BFSR.IBUSERR) on first CCM instruction fetch |

The exact same source runs correctly from SRAM2 but faults the instant it
fetches an instruction from CCM on this board. CCM **data** access works and
the MPU is disabled, so this is recorded as an observed board behavior, not a
proven root cause.

## Build

Default (SRAM2):

```bash
mkdir -p build && cd build
cmake -G Ninja ..
ninja
ninja flash          # probe-rs / ULINK2 (SWD)
```

To test CCM instead, point `BOARD_LINKER_SCRIPT` at
`stm32f407ram_test_ccm.ld` (in `CMakeLists.txt`) and rebuild in a fresh dir.

Console: board USART3 / USB-serial (115200 8-N-1).
