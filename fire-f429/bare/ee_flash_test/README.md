# AT24C02 EEPROM test - fire-f429

The test uses I2C1 on PB6 (SCL) / PB7 (SDA) at 400 kHz. The AT24C02 address
pins A2/A1/A0 are grounded, so the device address is `0x50`. It erases all
256 bytes (programs `0xFF`), programs a deterministic pattern, reads it back,
and verifies every byte. Writes use 8-byte pages with ACK polling.

The bare project stores the test buffers in internal SRAM. The matching
`app/ee_flash_test` stores the buffers in remapped SDRAM. Both use the same
I2C clock and command sequence, so the comparison shows the effect of CPU
buffer placement on EEPROM transfers.

## Result

Measured on fire-f429 hardware at 180 MHz CPU and 400 kHz I2C1:

| Operation | Bare internal SRAM | SDRAM app |
| --------- | ------------------ | --------- |
| Erase 256 B | 10,313,484 cycles | 10,316,539 cycles |
| Program 256 B | 4,468 B/s | 4,466 B/s |
| Read 256 B | 42,550 B/s | 42,543 B/s |
| Verify | PASS (0 errors) | PASS (0 errors) |

The SDRAM-buffered results are within 0.1% of the internal-SRAM results.
EEPROM write time is dominated by the internal page-write cycle and ACK
polling, and read time by the I2C clock, so CPU buffer placement has no
significant effect.

## Build and flash

```bash
cmake -G Ninja -B build .
ninja -C build
ninja -C build flash
```