# AT24C02 EEPROM test - fire-f429 SDRAM app

This test is the SDRAM-buffered counterpart of `bare/ee_flash_test`. It uses
I2C1 on PB6/PB7 at 400 kHz, device address `0x50`, erases all 256 bytes,
programs a deterministic pattern, reads it back, and verifies every byte. The
test buffers are placed in remapped SDRAM.

## Result

Measured on fire-f429 hardware at 180 MHz CPU and 400 kHz I2C1:

| Operation | Result |
| --------- | ------ |
| Erase 256 B | 10,316,539 cycles |
| Program 256 B | 4,466 B/s |
| Read 256 B | 42,543 B/s |
| Verify | PASS (0 errors) |

Compare with `../ee_flash_test/README.md` after running both images. EEPROM
write time is dominated by the internal page-write cycle and ACK polling, so
buffer placement should have little effect; read speed is limited by the I2C
clock.

Measured results confirm this: program and read are within 0.1% of the bare
internal-SRAM test, and both pass verification with zero errors.

## Build and flash

```bash
cmake -G Ninja -B build .
ninja -C build
ninja -C build flash
```