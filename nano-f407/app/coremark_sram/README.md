# CoreMark 1.0.1 from SRAM2 @ 168 MHz — nano-f407 (STM32F407VET6)

Runs CoreMark 1.0.1 (**10,000 iterations**) with the timed benchmark kernel
(`core_*.c`) linked into **SRAM2** (0x2001C000, 16 KB) and copy-in'd from
flash at startup; the harness/HAL/printf stay in flash. Goal: isolate pure
CPU+SRAM throughput from flash wait-state / ART influence. Timed with the DWT
cycle counter (see `src/core_portme.c`).

## Test status (measured on hardware, nano-f407 — healthy chip)

| Item | Result |
| ---- | ------ |
| SRAM2 execution | **Works** — full run completes |
| Iterations/Sec | **401.75** (GCC 15.3.1 `-Ofast -ffp-contract=fast -funroll-loops`) |
| CRC (crcfinal) | **0x988c** — `Correct operation validated` |
| Compare: same build in flash | 427.75 iterations/s |

Executing the kernel from SRAM2 is functional and correct, though slightly
slower (401.75 vs 427.75) than from flash on this board. The result matches
the SRAM2 run on the dev1-f407 (401.75), confirming it is reproducible.

> CCM (0x10000000) cannot run code on this part — see `ccm_probe` and the
> board README "RAM / CCM code-execution test status".

## Build

Requires the CMake/Ninja environment from the board-level `../../README.md`.

```bash
mkdir -p build && cd build
cmake -G Ninja ..
ninja
ninja flash          # programs via probe-rs / ST-Link (SWD)
```

Console is the board's USART1 / ST-Link VCP (115200 8-N-1); on this setup
reachable via the CH340 USB-serial port (auto-detect, e.g. COM4) — see
`../../README.md`.
