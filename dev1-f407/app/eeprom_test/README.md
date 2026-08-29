# AT24C02 EEPROM test - dev1-f407

The test uses I2C1 on PB8 (SCL) / PB9 (SDA) at 400 kHz. The AT24C02 address
pins A2/A1/A0 are grounded, so the device address is `0x50`. It erases all
256 bytes (programs `0xFF`), programs a deterministic pattern, reads it back,
and verifies every byte. Writes use 8-byte pages with ACK polling.

## Result

Measured on dev1-f407 hardware at 168 MHz CPU and 400 kHz I2C1:

| Operation | Cycles | Throughput |
| --------- | ------ | ---------- |
| Erase 256 B | 6,426,306 | — |
| Program 256 B | 6,426,238 | 6,692 B/s |
| Read 256 B | 980,355 | 43,869 B/s |
| Verify | PASS (0 errors) | — |

EEPROM write time is dominated by the internal page-write cycle and ACK
polling; read time by the I2C clock.

## Build and flash

```bash
cmake -G Ninja -B build .
ninja -C build
ninja -C build flash
```

The `flash` target auto-detects the debug probe (ULINK2 or ST-Link V2). To
force a specific probe, pass `-DPROBE_SELECTOR=0483:3752:...` (ST-Link V2) or
`-DPROBE_SELECTOR=c251:2722:V0010M9E` (ULINK2) at configure time.