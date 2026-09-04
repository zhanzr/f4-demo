# blink_hello_48m — dev1-f407 (STM32F407VET6)

Identical in behaviour to **blink_hello**, except this project forces the MCU
to run at **48 MHz** instead of the board default **168 MHz**.

Blinks the three on-board LEDs (PE13/14/15) in sequence and, once per second,
samples the three internal channels of the master **ADC1** peripheral and
prints their raw codes plus the converted engineering values (supply voltage,
junction temperature, VBAT) over the board console — exactly like blink_hello.

## Why / how the clock is overridden

The shared board layer (`dev1-f407/board/board.c`) keeps every project at the
default **168 MHz** (PLLN=336, PLLP=2). Its `SystemClock_Config()` is declared
**weak**, so a project may supply its own *strong* `SystemClock_Config()` to
override it only for that project.

This project does that in **`src/clock_config.c`**, running at 48 MHz from the
25 MHz HSE crystal:

```
PLLM=25  -> PLL input  1 MHz
PLLN=192 -> VCO       192 MHz
PLLP=4   -> SYSCLK     48 MHz
PLLQ=4   -> PLL48CLK  48 MHz  (USB/SDIO, not used here)
AHB=48 MHz, APB1=24 MHz (/2), APB2=48 MHz (/1)
Flash latency 1 wait state, regulator scale 1
```

`Board_Init()` calls this project's `SystemClock_Config()`, so **only**
blink_hello_48m runs at 48 MHz. Every other dev1-f407 project keeps the 168 MHz
default untouched.

> PLLN=192 / PLLP=4 keeps the VCO at 192 MHz, inside the F407's 100-432 MHz VCO
> range (a 48 MHz VCO does not lock reliably). APB1 is divided by 2 so it stays
> within the F407's 42 MHz APB1 maximum.

## What it demonstrates

- A project-local clock config that overrides the shared board default.
- Basic GPIO blink of LED1/2/3 (PE13/14/15, low-active).
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
  (`TS_CAL1` @ 30 °C / `TS_CAL2` @ 110 °C from system memory). At 48 MHz the
  ADC clock is PCLK2/4 = 12 MHz, and the 480-cycle sample time (~40 µs) is still
  well above the temp sensor's ~10 µs minimum.

Example output:

```
==== dev1-f407 (STM32F407VET6) blink_hello_48m @ 48 MHz ====
SYSCLK = 48000000 Hz (48 MHz)
blink: LED cycle 1 @ 1000 ms
ADC1: temp=978 code, VREFINT=1510 code, VBAT=240 code
     Vdda ~= 3281 mV, chip temp ~= 40 C, VBAT ~= 384 mV
```

## Build

Requires the CMake/Ninja environment from the board-level `../../README.md`.

```bash
mkdir -p build && cd build
cmake -G Ninja ..
ninja          # builds .elf / .hex / .bin
ninja flash    # programs the board via probe-rs (ULINK2 CMSIS-DAP / SWD)
```

The console is the board's UART (USART3, PD8/PD9) on the on-board RS232/RS485
transceivers at 115200 baud — see the board-level `../../README.md` for the
pins and a capture recipe.
