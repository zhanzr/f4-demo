# W25Q128FVSG SPI flash test - fire-f429

The test uses SPI5 on PF7/PF8/PF9 (SCK/MISO/MOSI, AF5) and PF6 as a
software-controlled chip select. It reads the JEDEC ID, erases one 64 KiB
sector, programs 64 KiB, reads it back, and verifies every byte.

The bare project stores the test buffers in internal SRAM. The matching
`app/spi_flash_test` stores the buffers in remapped SDRAM. Both use the same
SPI clock and flash command sequence, so the comparison shows the effect of
CPU buffer placement on SPI flash transfers.

## Result

Measured on fire-f429 hardware at 180 MHz CPU and 45 MHz SPI5 clock:

| Operation | Bare internal SRAM | SDRAM app |
| --------- | ------------------ | --------- |
| JEDEC ID  | `0xEF4018`         | `0xEF4018` |
| Erase 64 KiB | 38,537,603 cycles | 41,294,096 cycles |
| Program 64 KiB | 518.606 KiB/s | 448.018 KiB/s |
| Read 64 KiB | 1,237.488 KiB/s | 714.023 KiB/s |
| Verify     | PASS (0 errors)    | PASS (0 errors) |

The SDRAM-buffered program transfer is about 13.6% slower and the read
transfer about 42.3% slower. Erase differs by about 7.2%; erase is an
internal flash operation, so that difference is timing variation rather than
an effect of the CPU buffer location.

## Build and flash

```bash
cmake -G Ninja -B build .
ninja -C build
ninja -C build flash
```
