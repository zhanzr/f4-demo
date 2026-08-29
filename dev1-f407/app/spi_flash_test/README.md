# W25Q64 SPI flash test - dev1-f407

The test uses SPI2 on PB10 (SCK) / PC2 (MISO) / PC3 (MOSI) at 42 MHz (APB1
clock / 2). The W25Q64 chip-select is on PE3. It reads the JEDEC ID, erases
a 64 KiB sector, programs a 4 KiB deterministic pattern, reads it back, and
verifies every byte.

The test buffers live in internal SRAM (the dev1-f407 has no SDRAM), so the
test size is 4 KiB to keep both write+read buffers comfortably inside the
128 KB SRAM.

## Result

Measured on dev1-f407 hardware at 168 MHz CPU and 42 MHz SPI2:

| Operation | Cycles | Throughput |
| --------- | ------ | ---------- |
| JEDEC ID | `0xEF4017` (Winbond W25Q64) | — |
| Erase 64 KiB | 17,280,605 | — |
| Program 4 KiB | 2,035,313 | 330.170 KiB/s |
| Read 4 KiB | 869,718 | 772.664 KiB/s |
| Verify | PASS (0 errors) | — |

## Build and flash

```bash
cmake -G Ninja -B build .
ninja -C build
ninja -C build flash
```

The `flash` target auto-detects the debug probe (ULINK2 or ST-Link V2). To
force a specific probe, pass `-DPROBE_SELECTOR=0483:3752:...` (ST-Link V2) or
`-DPROBE_SELECTOR=c251:2722:V0010M9E` (ULINK2) at configure time.