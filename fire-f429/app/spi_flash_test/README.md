# W25Q128FVSG SPI flash test - fire-f429 SDRAM app

This test is the SDRAM-buffered counterpart of `bare/spi_flash_test`. It uses
SPI5 on PF7/PF8/PF9 and PF6 software chip select, then reads JEDEC ID, erases
one 64 KiB sector, programs 64 KiB, reads it back, and verifies every byte.
The 64 KiB transmit and receive buffers are placed in remapped SDRAM.

## Result

Measured on fire-f429 hardware at 180 MHz CPU and 45 MHz SPI5 clock:

| Operation | Result |
| --------- | ------ |
| JEDEC ID  | `0xEF4018` |
| Erase 64 KiB | 41,294,096 cycles |
| Program 64 KiB | 448.018 KiB/s |
| Read 64 KiB | 714.023 KiB/s |
| Verify     | PASS (0 errors) |

Compare with `../spi_flash_test/README.md` after running both images. Erase
latency is flash-internal and should be nearly identical; program/read speed is
normally dominated by the SPI clock, with buffer placement affecting CPU
handling and HAL polling overhead.

Compared with the bare internal-SRAM test, this run was 13.6% slower for
program and 42.3% slower for read. The JEDEC ID and data verification matched;
the difference is transfer overhead from SDRAM-resident CPU buffers.

## Build and flash

```bash
cmake -G Ninja -B build .
ninja -C build
ninja -C build flash
```
