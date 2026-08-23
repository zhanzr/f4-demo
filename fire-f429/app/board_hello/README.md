# board_hello — fire-f429 (STM32F429IGT6)

Board self-test: LED blink + the on-board sensors, reported over USART1
(115200). Runs from flash/SRAM with `.data/.bss` in SDRAM
(`DATA_IN_ExtSDRAM`, `stm32f429_sdram.ld`).

Renamed from `app/blink_hello` because it now covers more than a simple
blink + hello.

## Sensors

| Sensor          | Interface / pins                     | Output                                   |
| --------------- | ------------------------------------ | ---------------------------------------- |
| ADC1 internal   | VREFINT (IN17), temp (IN18), VBAT    | mV / °C / mV                             |
| GL5516 LDR      | PA4 (ADC1_IN4)                       | raw code + mV                            |
| DHT11           | PE2 (single-wire, open-drain)        | temperature / humidity                   |
| MPU6050         | I2C1: PB6=SCL, PB7=SDA; INT=PI1      | accel ±4g, gyro ±2000°/s                 |

LDR circuit: `[VDD] <=> GL5516 (light-strength resistor) <=> PA4 <=> 10 kΩ <=>
GND`. More light -> lower LDR resistance -> lower PA4 voltage.

## Measured on hardware

```
==== fire-f429 (STM32F429IGT6) board_hello @ 180 MHz ====
MPU6050: found (WHO_AM_I 0x68)
ADC : VREFINT=1498 (3307 mV), temp=1001 code (43 C), VBAT=1028 (2490 mV)
LDR : PA4 raw=1977, 1596 mV
DHT11: 31.8 C, 73.0 %RH
MPU60: accel    872   -178  16096, gyro    -30    -13     28
```

## Build and flash

```bash
cmake -G Ninja -B build .
ninja -C build
ninja -C build flash
```

## Notes

- DHT11 timing uses the DWT cycle counter for microsecond accuracy; the pin is
  open-drain with the internal pull-up (the module also has an on-board
  pull-up).
- MPU6050 config (same as the Wildfire reference): PWR_MGMT_1=0 wake,
  SMPLRT_DIV=7, DLPF 5 Hz, accel ±4g (8192 LSB/g), gyro ±2000°/s
  (16.4 LSB/(°/s)); WHO_AM_I must read 0x68.
- The PA4 LDR channel was added to `src/adc_internal.c` alongside the
  internal channels (single ADC1, one channel per conversion).
