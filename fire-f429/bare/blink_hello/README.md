# blink_hello — fire-f429 (STM32F429IGT6)

Bare-metal LED + ADC internal-channel demo for the **fire-f429** board
(STM32F429IGT6, **bare** metal: built-in flash + SRAM only). Blinks the four
on-board LEDs (all low-active) **one by one**, prints the 180 MHz core clock,
and every second samples the ADC1 internal channels and reports VREFINT /
junction temperature / VBAT over the board console (USART1 via the VCP).

## What it demonstrates

- Four LEDs blink one at a time: LED_R (PH10), LED_G (PH11), LED_B (PH12),
  LED_1 (PD12) — all low-active.
  > When the board's **DVI interface is used**, LED_R/G/B share its data lines
  > and must be disconnected via the onboard jumpers (see the board README).
- 180 MHz core clock banner.
- ADC1 internal channels (STM32F42x/F43x mapping):

  | Channel | What it is |
  | ------- | ---------- |
  | ADC1_IN17 | VREFINT (internal reference ~1.21 V) |
  | ADC1_IN18 | Temperature sensor (shared with VBAT) |
  | ADC1_IN18 | VBAT (selected after the temperature read) |

  Temperature uses the **factory-calibrated** TS_CAL1/TS_CAL2 values
  (0x1FFF7A2C/2E), and VBAT uses the F42x/F43x internal /3 divider.

Example output:

```
==== fire-f429 (STM32F429IGT6) blink_hello @ 180 MHz ====
SYSCLK = 180000000 Hz (180 MHz)
ADC: VREFINT=1497 code (3309 mV), temp=986 code (38 C), VBAT=1003 code (2430 mV)
```

## Build

Requires the CMake/Ninja environment from the board-level `../../README.md`.

```bash
mkdir -p build && cd build
cmake -G Ninja ..
ninja          # builds .elf / .hex / .bin
ninja flash    # programs the board via probe-rs / Keil ULINK2 (SWD)
```

The console is the board's UART on the CMSIS-DAP VCP — see the board-level
`../../README.md` for baud/pins and a capture recipe.