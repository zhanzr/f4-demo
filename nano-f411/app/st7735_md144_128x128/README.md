# st7735_md144_128x128 - ST7735S 1.44" 128x128 LCD on nano-f411

Drives the **MD144** 1.44" **128x128** module (**ST7735S** controller) on
the **nano-f411** board (STM32F411CEU6 @ 100 MHz) over a 4-wire SPI bus,
with two interchangeable drive methods compared back to back in one demo
loop: bit-banged GPIO ("soft SPI") and the SPI1 peripheral ("hardware SPI").

## Controller notes

- **ST7735S, 128x128**: the init sequence sets the panel's frame rate,
  power and gamma registers, MADCTL **0xC8** and 16-bit COLMOD (0x3A =
  0x05); no inversion command (this is a TN panel).
- **Geometry**: the ST7735S GRAM is 132x162 while the glass shows 128x128,
  so every draw window is offset by **`ROW_Pre = 32`** (with MADCTL 0xC8).

## Wiring

| LCD pin | MCU pin | Feature | HW SPI1 (AF5) role |
| ------- | ------- | ------- | ------------------ |
| SCL | PA5 | SPI clock | **SPI1_SCK** (hardware) |
| SDA | PA7 | SPI data out (MOSI) | **SPI1_MOSI** (hardware) |
| RES | PA6 | Reset | stays GPIO (SPI1_MISO unused - write-only panel) |
| DC  | PA4 | Data/command select | stays GPIO (SPI1_NSS unused), driven per byte/transfer |
| CS  | PB8 | Chip select | stays GPIO software CS (PB8 has no SPI1 AF) |
| BL  | PB9 | Backlight - **TIM4_CH4 PWM: 15% soft pass / 13% HW pass** | - |

## What it does

The demo loops forever, running the **same pattern set on BOTH drive
methods**:

1. Big-font banner **"now will do / soft SPI test"** (yellow on blue, 3 s),
   then the full pattern set on the **bit-banged** bus.
2. Big-font banner **"now will do / HW SPI1 test"** (black on cyan, 3 s),
   then the same pattern set on **SPI1 @ 25 MHz**.

Pattern set (per pass, live FPS counter throughout):

1. **Info page** (normal, then **inverted**): compiler, build date, CPU
   frequency, active drive method (soft bit-bang / HW SPI1 + clock), the IO
   map, live backlight duty (15%/13%) and the UID (7 compact lines sized
   for the 128 px width - 21 chars/line with the 6x12 font).
2. **TEST_STAND** (500 ms between screens): window-border **frame**,
   **16-level gray** horizontal bars, **color bands**, then full **red,
   green, blue, white, black** fills.
3. **Gradient** - animated HSV hue sweep across the full color wheel
   (1 s per sweep).
4. **LED test** - board LED PC13 on/off.

The FPS counter makes the bus difference obvious: single-digit fps on the
gradient over bit-bang vs well over 100 fps on HW SPI1.

## DisplayChar fixes

Two `LCD_DisplayChar` bugs fixed in `src/lcd.c` (the transparent path -
used by the FPS text - was always correct; both bugs were in the opaque
path, first exercised by the info page):

1. **Glyph buffer too small**: the fixed `Buff[6 * 12]` (72 entries)
   overflowed the stack with the 8x16 banner font (128 pixels) and hung
   the firmware. The buffer is now `Buff[8 * 16]` (largest font).
2. **Garbled opaque text**: the opaque path filled the buffer *linearly
   from the raw glyph bit stream*, but `LCD_CopyBuffer` writes it
   *row-major* to the panel. The 6x12 font packs each 6-pixel row into one
   byte (2 padding bits), so the padding bled into the next row and
   smeared every glyph. The opaque path now uses the same per-row mapping
   as the transparent path (`bytesPerRow = Sizes / Height`, bit 0 =
   leftmost).

## Backlight PWM

**TIM4_CH4 on PB9 (AF2)** at 1 kHz (`PSC=99`, `ARR=999`): **15 % duty
during the soft pass, 13 % during the HW pass** (re-set by `main()` per
phase; `Backlight_SetDuty()` rescales CCR for 0..100 %, `Backlight_GetDuty()`
reports the live duty shown on the info page).

## Build / flash / console

```bash
cd app/st7735_md144_128x128
bash build.sh          # == mkdir build && cd build && cmake -G Ninja .. && ninja
ninja flash            # probe-rs download + reset over ST-Link SWD
```

Console is the board's USART1 / ST-Link VCP (COM9, 115200 8-N-1); a banner
prints once at boot and the pattern phases log as they run, looping forever.

> **"Target voltage (VAPP) is 0.02 V" warning** during `ninja flash` can be
> ignored - it is just the ST-Link's target-voltage sensing reporting an
> unreliable reading. Flashing, reset and the VCP console all work fine
> (verified on hardware).

## Hardware SPI

- **SPI1 AF5** on PA5 (SCK) / PA7 (MOSI), mode 3 (CPOL=1, CPHA=1 - the
  same idle-high / rising-edge-sampling timing the bit-bang produces),
  8-bit MSB-first, NSS soft. RES/DC/CS stay GPIO.
- **Clock**: APB2 = 100 MHz (this project overrides the weak
  `SystemClock_Config()` to run PCLK2 at the F411 max), default prescaler
  **/4 = 25 MHz SCK**. **/2 = 50 MHz** (the F411 SPI1 max) and
  **/8 = 12.5 MHz** are available via `LCD_SPI1_PRESC` if a module needs
  them. The core stays at 100 MHz; USART1 recomputes its baud from the
  live PCLK2, so the console stays at 115200.
- **Bus switching**: `LCD_UseSoftBus()` / `LCD_UseHwBus()` re-mux PA5/PA7
  (GPIO vs AF5), then `LCD_Reinit()` (reset + init sequence) re-frames the
  panel for the freshly selected bus. HW raster bursts stream through a
  512-byte TX buffer (one `HAL_SPI_Transmit` per <=512 bytes); commands
  stay unbuffered single-byte transmits.

## Files

- `src/main.c` - pattern set on both buses: banner, info (normal +
  inverted), TEST_STAND, HSV gradient, LED test, FPS counter
- `src/lcd.c` / `lcd.h` - ST7735S init + 128x128/ROW_Pre=32 geometry +
  drawing API + `LCD_Reinit`
- `src/lcd/lcd_fonts.c` / `lcd_fonts.h` - ASCII 6x12 font
- `src/lcd/lcd_font_1608.c` / `lcd_font_1608.h` - ASCII 8x16 banner font
- `src/interface.c` / `interface.h` - bus primitives: bit-banged 4-wire SPI
  **and** HW SPI1 (mode 3, buffered raster bursts) + bus switching
- `src/blockwrite/blockwrite.h` - pixel-window helper
- `src/backlight.c` / `backlight.h` - PB9/TIM4_CH4 PWM (+ duty getter)