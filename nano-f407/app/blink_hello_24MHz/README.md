# blink_hello_24MHz — nano-f407 (STM32F407VET6)

Identical in behaviour to **blink_hello**, except this project forces the
MCU to run at **24 MHz** instead of the board default **168 MHz**.

Blinks the single on-board LED and, once per second, samples the three
internal channels of the master **ADC1** peripheral and prints their raw codes
plus the converted engineering values (supply voltage, junction temperature,
VBAT) over the board console — exactly like blink_hello.

## Why / how the clock is overridden

The shared board layer (`nano-f407/board/board.c`) keeps every project at the
default **168 MHz** (PLLN=336, PLLP=2). Its `SystemClock_Config()` is declared
**weak**, so a project may supply its own *strong* `SystemClock_Config()` to
override it only for that project.

This project does that in **`src/clock_config.c`**, running at 24 MHz:

```
PLLM=8   -> PLL input  1 MHz
PLLN=192 -> VCO       192 MHz
PLLP=8   -> SYSCLK     24 MHz
AHB=24 MHz, APB1=24 MHz (/1), APB2=24 MHz (/1)
Flash latency 0, regulator scale 1
```

`Board_Init()` calls this project's `SystemClock_Config()`, so **only**
blink_hello_24MHz runs at 24 MHz. Every other nano-f407 project keeps the
168 MHz default untouched.

> PLLN=192 / PLLP=8 keeps the VCO at 192 MHz, inside the F407's 100-432 MHz
> VCO range (a 48 MHz VCO does not lock reliably).

## What it demonstrates

- A project-local clock config that overrides the shared board default.
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
  (`TS_CAL1` @ 30 °C / `TS_CAL2` @ 110 °C from system memory). At 24 MHz the
  ADC clock is PCLK2/4 = 6 MHz, and the 480-cycle sample time (~80 µs) is still
  well above the temp sensor's ~10 µs minimum.

Example output:

```
==== nano-f407 (STM32F407VET6) blink_hello_24MHz @ 24 MHz ====
SYSCLK = 24000000 Hz (24 MHz)
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
