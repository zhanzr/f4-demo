# SDRAM test - fire-f429

Runtime FMC test for the onboard **IS42S16400J** SDRAM: 8 MiB, 16-bit,
configured for 90 MHz FMC clock from the board's 180 MHz HCLK. The project
uses the normal flash/SRAM linker layout and accesses SDRAM directly at
`0xD0000000` (FMC bank 2); it does not remap code or data into external memory.

The test initializes the SDRAM, writes a deterministic 16-bit pattern across
the full 8 MiB, reads it back, checks every word, and reports DWT cycle counts
and calculated throughput over USART1 at 115200 baud.

## Result

Measured on fire-f429 hardware:

| Operation | Cycles | Throughput |
| --------- | ------ | ---------- |
| Write     | 25,170,005  | 57.040 MiB/s |
| Read      | 101,386,909 | 14.033 MiB/s |

Hardware result: `PASS (0 errors)` on 2026-08-23.

The result is printed by the firmware as `PASS` or `FAIL` with the error count.

## Build and flash

```bash
mkdir -p build && cd build
cmake -G Ninja ..
ninja
ninja flash
```

Connect to USART1 (PA9 TX, 115200 8-N-1) and reset the board to capture the
result. The project uses the vendor project's bank-2 configuration and the pin
mapping listed in the project request.
