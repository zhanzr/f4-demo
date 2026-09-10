# AT24C02 EEPROM test - apollo-f429

The test uses I2C2 on PH4 (SCL) / PH5 (SDA) at 400 kHz. The AT24C02 address
pins A2/A1/A0 are grounded, so the device address is `0x50`. It erases all
256 bytes (programs `0xFF`), programs a deterministic pattern, reads it back,
and verifies every byte. Writes use 8-byte pages with ACK polling.

The bare project stores the test buffers in internal SRAM. The matching
`app/ee_flash_test` stores the buffers in remapped SDRAM. Both use the same
I2C clock and command sequence, so the comparison shows the effect of CPU
buffer placement on EEPROM transfers.

## Result

Measured on apollo-f429 hardware at 180 MHz CPU and 400 kHz I2C2:

| Operation | Bare internal SRAM |
| --------- | ------------------ |
| Erase 256 B | 7,740,135 cycles |
| Program 256 B | 5,894 B/s |
| Read 256 B | 43,297 B/s |
| Verify | PASS (0 errors) |

## Build and flash

```bash
cmake -G Ninja -B build .
ninja -C build
ninja -C build flash
```