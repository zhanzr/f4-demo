# ili9163c_md144_128x128 - ILI9163C 1.44" 128x128 LCD on nano-f411

Drives an **ILI9163C** 1.44" **128x128** module (MD144-form-factor
connector) on the **nano-f411** board (STM32F411CEU6 @ 100 MHz) over a
**3-wire serial bus using the soft (bit-banged) method**.

## Controller notes

- **ILI9163C**: the init sequence sets the default gamma (0x26), frame
  rate (0xB1), power (0xC0/0xC1), VCOM (0xC5/0xC7), 16-bit COLMOD
  (0x3A = 0x05), MADCTL **0x08** (the display is rotated **180 degrees**
  vs the controller's 0xC8 default - MY+MX toggled with BGR kept, a true
  content rotation, not a mirror), source output direction (0xB7), gamma
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

## Wiring (no connection change needed for HW SPI)

| LCD pin | MCU pin | Feature | HW SPI1 (AF5) role |
| ------- | ------- | ------- | ------------------ |
| SCL | PA5 | SPI clock | **SPI1_SCK** (hardware) |
| SDA | PA7 | SPI data | **SPI1_MOSI** (hardware) |
| RES | PA6 | Reset | stays GPIO (SPI1_MISO unused - write-only panel) |
| CS  | PB8 | Chip select | stays GPIO software CS (PB8 has no SPI1 AF) |

(D/C is not wired - it is clocked as the first bit of each 9-bit frame.)

## What it does

The demo loops forever, running the **same pattern set on BOTH drive
methods**:

1. Big-font banner **"now will do / soft SPI test"** (yellow on blue, 3 s),
   then the full pattern set on the **bit-banged** bus.
2. Big-font banner **"now will do / HW SPI1 test"** (black on cyan, 3 s),
   then the same pattern set on **SPI1 @ 12.5 MHz**.

Pattern set (per pass, live FPS counter throughout):

1. **Info page** (normal, then **inverted**): compiler, build date, CPU
   frequency, drive method (soft bit-bang / HW SPI1), the IO map and the
   UID (7 compact lines sized for the 128 px width - 21 chars/line with
   the 6x12 font).
2. **TEST_STAND** (500 ms between screens): window-border **frame**,
   **16-level gray** horizontal bars, **color bands**, then full **red,
   green, blue, white, black** fills.
3. **Gradient** - animated HSV hue sweep across the full color wheel
   (1 s per sweep).
4. **LED test** - board LED PC13 on/off.

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

## Hardware SPI (implemented, packed 9-bit frames)

The pins are HW-SPI compatible - **no connection change needed**: PA5 is
SPI1_SCK and PA7 is SPI1_MOSI at AF5, exactly like the board's other LCD
projects.

The catch is the protocol: the F411 SPI only produces **8- or 16-bit**
frames (the DFF register has no 9-bit setting), while this module needs
9-bit frames. The HW path therefore transmits **16-bit words packed with
the 9-bit-frame bitstream**: frames are appended MSB-first into a bitstream
accumulator that drains into 16-bit words. The panel only observes SCL/SDA
and counts its own 9-bit boundaries, so the packing is transparent to it;
at the end of a burst the pending bits are always < 9 (they cycle
7-5-3-1-8-6-4-2-0 as frames accumulate), so an extra frame can never
complete - the panel discards the partial shift-register content when CS
rises.

- **Timing**: SPI mode 3 (CPOL=1, CPHA=1) - byte-for-byte the same
  idle-high / rising-edge-sampling timing the soft path produces.
- **Clock**: APB2 = 100 MHz (project clock override), default prescaler
  **/8 = 12.5 MHz SCK** (within the panel's write-cycle spec);
  `LCD_SPI1_PRESC=SPI_BAUDRATEPRESCALER_4` raises it to 25 MHz.
- **Bus switching**: `LCD_UseSoftBus()` / `LCD_UseHwBus()` re-mux PA5/PA7
  (GPIO vs AF5), then `LCD_Reinit()` re-frames the panel for the freshly
  selected bus. HW bursts pack into a 256-word TX buffer (one
  `HAL_SPI_Transmit` per <=256 words); commands stay single-frame
  transmits.

## Files

- `src/main.c` - pattern set on both buses: banner, info (normal +
  inverted), TEST_STAND, HSV gradient, LED test, FPS counter
- `src/lcd.c` / `lcd.h` - ILI9163C init (MADCTL 0x08, 180 deg rotated) +
  128x128/COL_Pre=ROW_Pre=0 geometry + drawing API + `LCD_Reinit`
- `src/lcd/lcd_fonts.c` / `lcd_fonts.h` - ASCII 6x12 font
- `src/lcd/lcd_font_1608.c` / `lcd_font_1608.h` - ASCII 8x16 banner font
- `src/interface.c` / `interface.h` - 3-wire 9-bit primitives: soft
  bit-bang **and** HW SPI1 (mode 3, packed 16-bit words) + bus switching
- `src/blockwrite/blockwrite.h` - pixel-window helper