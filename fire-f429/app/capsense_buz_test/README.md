# Capsense + buzzer test - fire-f429 (app)

Drives the active buzzer (PI11) from the capacitive touch pad (PA5).

## Behavior

- Touch and hold the capsense pad -> buzzer ON.
- Release the pad -> buzzer OFF, but the buzzer stays on for **at least
  500 ms** from the press (minimum ON period), even for a very short tap.

## Wiring

| Function          | Pin  | Note                                  |
| ----------------- | ---- | ------------------------------------- |
| Capacitive pad    | PA5  | TIM2_CH1 input capture (AF1)          |
| Active buzzer     | PI11 | base of an NPN BJT; HIGH = buzzer ON  |

## How the capsense works

Ported from the Wildfire (野火) F429 "TIM-电容按键" example
(`User/TouchPad/bsp_touchpad.c`):

- The pad's capacitance charges through the on-board series resistor toward
  3.3 V; the GPIO Schmitt trigger trips at ~V_IH and the rising edge is
  latched by TIM2_CH1 input capture. A finger adds ~10-30 pF -> longer R*C
  charge time -> larger captured counter value.
- Each measurement: drive PA5 low (pulldown) for 5 ms to fully discharge,
  clear the capture flag, CNT = 0, then PA5 back to AF input and wait for the
  rising edge.
- TIM2: APB1 45 MHz * 2 = 90 MHz, PSC = 23 -> 3.75 MHz counter (~266.7 ns),
  ARR = 0xFFFF.
- Calibration at startup: 10 samples, sort, average the middle 6 -> baseline.
- Scan: max-of-3 samples; pressed if raw > baseline + 100 (and < 10x
  baseline); 2 consecutive scans debounce the state.

Measured on hardware: baseline ~145 ticks idle, stable at 145-146.

## Result (verified on hardware)

- Calibration succeeds (`capsense: baseline=145`, `capsense: ready`).
- Press -> buzzer sounds; release -> buzzer stops after the 500 ms minimum.

## Build and flash

```bash
cmake -G Ninja -B build .
ninja -C build
ninja -C build flash
```

Serial console: USART1 115200 (COM36).
