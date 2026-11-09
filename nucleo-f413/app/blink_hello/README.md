# blink_hello - Nucleo-F413ZH (STM32F413ZHT6)

First project on the **nucleo-f413** board: blinks the three user LEDs
and periodically samples the ADC1 internal channels, printing over the
ST-Link virtual COM port. Follows the `nano-f411` blink_hello project.

## What it does

- **LD1 (PB0, green)** blinks every 250 ms phase, **LD2 (PB7, blue)**
  every 2nd phase, **LD3 (PB14, red)** every 4th phase (all high-active)
- Every ~1 s prints the ADC1 internal-channel readings:
  chip temperature (factory-calibration interpolated), VREFINT-derived
  supply voltage and VBAT
- Banner with SYSCLK and flash latency at boot

## Board facts

- STM32F413ZHT6 @ **96 MHz from the 8 MHz ST-LINK MCO** (HSE bypass;
  the Nucleo-144 does not fit an HSE crystal)
- LEDs: LD1=PB0, LD2=PB7, LD3=PB14 (high-active)
- USER button B1 = PC13 (pressed = high)
- Console: **USART3 PD8 (TX) / PD9 (RX)** - ST-LINK VCP, 115200 8-N-1
- Debug: onboard ST-Link over SWD (probe-rs auto-detects)

## Build / flash

```bash
cd app/blink_hello
bash build.sh          # == mkdir -p build && cd build && cmake -G Ninja .. && ninja
ninja flash            # probe-rs download + reset over ST-Link SWD
```

> **"Target voltage (VAPP) is 0.02 V" warning** during `ninja flash`
> can be ignored - it is just the ST-Link's target-voltage sensing
> reporting an unreliable reading.

## Files

- `src/main.c` - 3-LED blink + ADC report loop
- `src/adc_internal.c/.h` - ADC1 internal-channel sampler (copied from
  the nano-f411 project; F41x family ADC)
