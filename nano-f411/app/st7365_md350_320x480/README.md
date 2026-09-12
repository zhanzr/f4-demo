# st7365_md350_320x480 - ST7365P 3.5" 320x480 LCD on nano-f411

Drives the **MD350** 3.5" **320x480** module (**ST7365P** controller,
ST7796-register-compatible) on the **nano-f411** board (STM32F411CEU6 @
100 MHz) over a 4-wire SPI bus, driven by the **soft (bit-banged)**
method.

## Controller notes

- **ST7365P**: the init sequence enables the command-set registers
  (0xF0 = 0xC3 / 0x96 ... 0x3C / 0x69), sets MADCTL **0x48** (portrait,
  BGR) and 16-bit COLMOD (0x3A = 0x05), then the display-output control
  (0xE8), VCOM (0xC5), power (0xC2) and positive/negative gamma tables
  (0xE0/0xE1).
- **Geometry**: 320x480, windows at **`COL_Pre = 0`, `ROW_Pre = 0`** (the
  controller addresses the full range directly).
- **Reset**: RST high 20 ms -> low 20 ms -> high 200 ms, 100 ms before the
  sleep-out command.

## Wiring

| LCD pin | MCU pin | Feature |
| ------- | ------- | ------- |
| SCL | PA5 | SPI clock (bit-banged) |
| SDA | PA7 | SPI data out (MOSI) |
| RES | **PA3** | Reset |
| DC  | PA4 | Data/command select |
| CS  | PB8 | Chip select |
| BL  | PB9 | Backlight - **TIM4_CH4 PWM, 15%** |
| MISO | PA6 | Module read-back line (**unused** by this write-only soft driver; maps to SPI1_MISO at AF5 for a future HW SPI1 path) |

## What it does

The demo loops forever on the soft bus:

1. Big-font banner **"MD350 320x480 / soft SPI test"** (yellow on blue,
   3 s).
2. **Info page** (normal, then **inverted**): compiler, build date, CPU
   frequency, drive method, the IO map, live backlight duty and the UID.
3. **TEST_STAND** (500 ms between screens): window-border **frame**,
   **16-level gray** horizontal bars, **color bands**, then full **red,
   green, blue, white, black** fills.
4. **Gradient** - animated HSV hue sweep across the full color wheel
   (4 s per sweep).
5. **LED test** - board LED PC13 on/off.

All with a **live FPS counter** drawn transparently in the bottom band.

Note: a 320x480 frame is 153,600 pixels (300 KB) - over the bit-banged
bus the full-window fills and gradient frames take on the order of a
second each (single-digit fps), which is inherent to the soft method at
this panel size.

## Backlight PWM

**TIM4_CH4 on PB9 (AF2)** at 1 kHz (`PSC=99`, `ARR=999`), **15 % duty**
(set at boot; `Backlight_SetDuty()` rescales CCR for 0..100 %,
`Backlight_GetDuty()` reports the live duty shown on the info page).

## Build / flash / console

```bash
cd app/st7365_md350_320x480
bash build.sh          # == mkdir build && cd build && cmake -G Ninja .. && ninja
ninja flash            # probe-rs download + reset over ST-Link SWD
```

Console is the board's USART1 / ST-Link VCP (COM9, 115200 8-N-1); a banner
prints once at boot and the pattern phases log as they run, looping forever.

> **"Target voltage (VAPP) is 0.02 V" warning** during `ninja flash` can be
> ignored - it is just the ST-Link's target-voltage sensing reporting an
> unreliable reading. Flashing, reset and the VCP console all work fine
> (verified on hardware).

## Hardware SPI (future work)

Not implemented yet (the soft method is the current focus). The module's
MISO line maps to PA6 = SPI1_MISO at AF5, so a future HW path can run
SPI1 in full duplex (SCK=PA5, MOSI=PA7, MISO=PA6) - useful for panel ID
read-back as well as faster fills. The panel protocol (4-wire with a DC
pin) needs no framing tricks - plain 8-bit frames, same as the board's
other 4-wire LCD projects.

## Files

- `src/main.c` - pattern set on the soft bus: banner, info (normal +
  inverted), TEST_STAND, HSV gradient, LED test, FPS counter
- `src/lcd.c` / `lcd.h` - ST7365P init + 320x480/COL_Pre=ROW_Pre=0
  geometry + drawing API + `LCD_Reinit`
- `src/lcd/lcd_fonts.c` / `lcd_fonts.h` - ASCII 6x12 font
- `src/lcd/lcd_font_1608.c` / `lcd_font_1608.h` - ASCII 8x16 banner font
- `src/interface.c` / `interface.h` - 4-wire soft-SPI primitives
- `src/blockwrite/blockwrite.h` - pixel-window helper
- `src/backlight.c` / `backlight.h` - PB9/TIM4_CH4 PWM (+ duty getter)