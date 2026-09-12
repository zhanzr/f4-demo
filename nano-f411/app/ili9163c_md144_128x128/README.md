# ili9163c_md144_128x128 - ILI9163C 1.44" 128x128 LCD on nano-f411

Drives an **ILI9163C** 1.44" **128x128** module (MD144-form-factor
connector) on the **nano-f411** board (STM32F411CEU6 @ 100 MHz) over a
**3-wire serial bus using the soft (bit-banged) method**.

## Controller notes

- **ILI9163C**: the init sequence sets the default gamma (0x26), frame
  rate (0xB1), power (0xC0/0xC1), VCOM (0xC5/0xC7), 16-bit COLMOD
  (0x3A = 0x05), MADCTL 0xC8, source output direction (0xB7), gamma
  enable (0xF2) and the positive/negative gamma tables (0xE0/0xE1).
- **Geometry**: 128x128, windows at **`COL_Pre = 0`, `ROW_Pre = 0`** (the
  init addresses columns 0..127 and pages 0..159 directly).

## 3-wire serial (no D/C pin)

The module connector has **no D/C pin** and **no backlight pin**:

- The panel is strapped for **3-wire serial**: every byte is a **9-bit
  frame** - the D/C bit (0 = command, 1 = data) is clocked first, then the
  8 data bits, MSB first. The D/C level travels in-protocol; no GPIO is
  used for it.
- The **backlight is hardwired on-module** - it is on whenever the module
  is powered; there is no brightness control.

## Wiring

| LCD pin | MCU pin | Feature |
| ------- | ------- | ------- |
| SCL | PA5 | SPI clock (bit-banged) |
| SDA | PA7 | SPI data (bit-banged) |
| RES | PA6 | Reset |
| CS  | PB8 | Chip select |

(D/C is not wired - it is clocked as the first bit of each 9-bit frame.)

## What it does

The demo loops forever on the soft bus:

1. Big-font banner **"now will do / soft SPI test"** (yellow on blue, 3 s).
2. **Info page** (normal, then **inverted**): compiler, build date, CPU
   frequency, drive method, the IO map and the UID (7 compact lines sized
   for the 128 px width - 21 chars/line with the 6x12 font).
3. **TEST_STAND** (500 ms between screens): window-border **frame**,
   **16-level gray** horizontal bars, **color bands**, then full **red,
   green, blue, white, black** fills.
4. **Gradient** - animated HSV hue sweep across the full color wheel
   (1 s per sweep).
5. **LED test** - board LED PC13 on/off.

All with a **live FPS counter** drawn transparently in the bottom band.

## Build / flash / console

```bash
cd app/ili9163c_md144_128x128
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

The HW SPI1 path is not implemented for this module yet. It would require
**9-bit SPI frames** (`SPI_DATASIZE_9BIT` with the D/C bit as the MSB of
each frame, CS-framed per byte) - the byte-level plumbing differs from the
8-bit 4-wire drivers used by the other LCD projects on this board.

## Files

- `src/main.c` - pattern set on the soft bus: banner, info (normal +
  inverted), TEST_STAND, HSV gradient, LED test, FPS counter
- `src/lcd.c` / `lcd.h` - ILI9163C init + 128x128/COL_Pre=ROW_Pre=0
  geometry + drawing API + `LCD_Reinit`
- `src/lcd/lcd_fonts.c` / `lcd_fonts.h` - ASCII 6x12 font
- `src/lcd/lcd_font_1608.c` / `lcd_font_1608.h` - ASCII 8x16 banner font
- `src/interface.c` / `interface.h` - 3-wire 9-bit soft-SPI primitives
- `src/blockwrite/blockwrite.h` - pixel-window helper