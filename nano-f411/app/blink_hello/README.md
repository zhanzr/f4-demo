# blink_hello — nano-f411 (STM32F411CEU6)

LED blink + ADC internal-channel demo for the **nano-f411** board
(STM32F411CEU6 @ 100 MHz). Blinks the single on-board LED and, once per
second, samples the internal channels of the **ADC1** peripheral and prints
their raw codes plus the converted engineering values (supply voltage,
junction temperature, VBAT) over the board console.

## What it demonstrates

- Basic GPIO blink (the board's LED, PC13, low-active).
- The **F411 internal-channel layout**, which differs from the F407:

  | Channel | What it is |
  | ------- | ---------- |
  | ADC1_IN17 | VREFINT (internal reference, ~1.21 V) |
  | ADC1_IN18 | Temperature sensor **or** VBAT (shared input) |

  > On F411 the temperature sensor and VBAT are **both** on IN18, selected by
  > the `TSVREFE` / `VBATE` bits in `ADC1->CCR` (mutually exclusive; the
  > driver cannot enable both). The temp sensor also gates VREFINT, so VBAT is
  > converted in a separate pass while `TSVREFE` is off. On the F407 these were
  > three independent channels (temp on IN16) convertible in one scan.

- VREFINT is used to back out the actual supply voltage, which then scales the
  temperature and VBAT readings.
- The junction temperature uses the **factory-calibrated** temp-sensor values
  (`TS_CAL1` @ 30 °C / `TS_CAL2` @ 110 °C from system memory), and the internal
  channels are sampled with a **480-cycle** sample time (38.4 µs @ 12.5 MHz ADC
  clock; the F4 temp sensor needs ~10 µs minimum).
- VBAT is scaled by **4**: on F411 the VBAT input passes a 1/4 internal divider
  (older F407 parts use /2).

Example output:

```
ADC1: temp=1000 code, VREFINT=1504 code, VBAT=790 code
     Vdda ~= 3296 mV, chip temp ~= 44 C, VBAT ~= 2542 mV
```

## Build

Requires the CMake/Ninja environment from the board-level `../../README.md`.

```bash
mkdir -p build && cd build
cmake -G Ninja ..
ninja          # builds .elf / .hex / .bin
ninja flash    # programs the board via probe-rs / ST-Link (SWD)
```

The console is the board's UART on the ST-Link virtual COM port — see the
board-level `../../README.md` for the baud/pins and a capture recipe.