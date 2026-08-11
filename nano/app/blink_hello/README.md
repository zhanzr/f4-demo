# blink_hello — nano (STM32F407VET6)

LED blink + ADC internal-channel demo for the **nano** board (STM32F407VET6
@ 168 MHz). Blinks the single on-board LED and, once per second, samples the
three internal channels of the master **ADC1** peripheral and prints their raw
codes plus the converted engineering values (supply voltage, junction
temperature, VBAT) over the board console.

## What it demonstrates

- Basic GPIO blink (the board's LED).
- ADC1 scan of the three internal channels:

  | Channel | What it is |
  | ------- | ---------- |
  | ADC1_IN16 | Temperature sensor |
  | ADC1_IN17 | VREFINT (internal reference, ~1.21 V) |
  | ADC1_IN18 | VBAT (battery/backup supply) |

  > These channels exist only on ADC1. This F407 is an **F40x/F41x** device, so
  > the temperature sensor is on **IN16** (not the shared IN18 of F42x/F43x).

- VREFINT is used to back out the actual supply voltage, which then scales the
  temperature and VBAT readings.
- The junction temperature uses the **factory-calibrated** temp-sensor values
  (`TS_CAL1` @ 30 °C / `TS_CAL2` @ 110 °C from system memory), and the internal
  channels are sampled with a **480-cycle** sample time (the F4 temp sensor
  needs ~10 µs; 480 cyc @ 21 MHz ADC clock = 22.9 µs). A short sample time
  (e.g. 84 cycles) yields an unrealistically cold reading.

Example output:

```
ADC1: temp=978 code, VREFINT=1510 code, VBAT=240 code
     Vdda ~= 3281 mV, chip temp ~= 40 C, VBAT ~= 384 mV
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
