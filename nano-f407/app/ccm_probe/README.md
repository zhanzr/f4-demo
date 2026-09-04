# ccm_probe — CCM instruction-fetch isolation (nano-f407, STM32F407VET6)

Focused experiment that isolates **why** executing code from CCM (0x10000000)
hard-faults. It works up the chain in a single run *before* calling the CCM
function, so each hypothesis is ruled in/out independently:

| Step | Probe | Result (this board) |
| ---- | ----- | ------------------- |
| 1 | CCM **data** write/read @0x10000100 | `0xDEADBEEF -> OK` (data bus reaches CCM) |
| 2 | Read back the code bytes at `&ccm_function` | valid instructions (e.g. `0xB047` = `BX LR`) — correct code is resident, not garbage |
| 3 | **Call** `ccm_function()` (fetch from CCM) | **BFSR.IBUSERR** hard fault (CFSR=0x00000100, HFSR=0x40000000) on the first fetch |

`src/ccm_func.c` holds only `ccm_function` (trivial `a + b`) in `.ram_code`,
and `stm32f407ccm_probe.ld` places that `.ram_code` in CCM. The harness
(`main`) stays in flash. It is compiled trivially and is **not** optimized
away (`-O1`) so the fetch path is exercised.

## Conclusion

The STM32F407 CPU can access CCM for **data** and the correct **code is
present** at the fetch address, but it **cannot fetch an instruction** from
0x10000000. This is the documented STM32F4 architecture: CCM RAM sits on the
Cortex-M4 **D-bus (data)** only, so the instruction (code) bus never reaches
CCM. CCM is therefore a **data-only** memory on these parts — code must run
from flash or SRAM1/SRAM2 (system bus). The identical fault reproduces on the
healthy nano-f407 and the dev1-f407, confirming it is a chip-level trait, not a
board defect.

## Build & run

```bash
bash build.sh          # or: mkdir build && cd build && cmake -G Ninja .. && ninja
ninja flash            # probe-rs / ST-Link (SWD)
```

Console: USART1 / ST-Link VCP (115200 8-N-1), reachable in this setup via the
CH340 USB-serial port (auto-detect, e.g. COM4). Steps 1–2 print, then the
fault traps in `HardFault_Handler`; read CFSR/HFSR via the debugger if needed.
