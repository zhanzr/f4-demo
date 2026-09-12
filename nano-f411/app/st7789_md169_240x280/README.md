# st7789_md169_240x280 - ST7789V/T3 1.69" 240x280 LCD on nano-f411

Drives an **ST7789T3** 1.69" **240x280 (MD169)** IPS module on the
**nano-f411** board (STM32F411CEU6 @ 100 MHz). The
`st7735_md144_128x128` project is the baseline (same wiring, same dual-bus
demo); the driving details (ST7789V init sequence, geometry) come from the
c542 `board_t1/bsp/st7789` driver used by `st7789_md169_240x280` there.

## Why the ST7735 firmware misdisplays this module

The ST7735 and ST7789 are **very similar** - same 4-wire serial protocol and
the same command structure (0x2A/0x2B/0x2C window addressing, 0x36 MADCTL,
0x3A COLMOD, 0x11 sleep out, 0x29 display on). What differs is exactly what
makes the ST7735 build show only a small, wrongly-oriented window here:

- **GRAM geometry**: ST7735 = 132x162 GRAM (1.44" glass offset inside),
  ST7789 = 240x320 GRAM. The st7735 init frames 128x128 windows at its
  offsets, so the ST7789 renders them in a corner of its GRAM.
- **Panel init**: the power/porch/gamma sequences (0xB2/0xBB/0xC3/0xE0...)
  are panel-specific and completely different.
- **Orientation**: MADCTL 0xC8 (ST7735 1.44") vs 0x00 (MD169).
- **IPS**: the MD169 is an ST7789T3 IPS panel and needs `INVON` (0x21).

## Wiring (single table: pin, feature, both drive methods)

Identical to the st7735_md144 baseline - **SDA and RES are swapped** vs the
vendor example (SDA PA6 -> PA7, RES PA7 -> PA6):

| LCD pin | MCU pin | Feature | HW SPI1 (AF5) role |
| ------- | ------- | ------- | ------------------ |
| SCL | PA5 | SPI clock | **SPI1_SCK** (hardware) |
| SDA | **PA7** | SPI MOSI - data out (vendor used PA6) | **SPI1_MOSI** (hardware) |
| RES | **PA6** | Reset (vendor used PA7) | stays GPIO (SPI1_MISO unused - write-only panel) |
| DC  | PA4 | Data/command select | stays GPIO (SPI1_NSS unused), driven per byte/transfer |
| CS  | PB8 | Chip select | stays GPIO software CS (PB8 has no SPI1 AF) |
| BL  | PB9 | Backlight - **TIM4_CH4 PWM: 15% soft pass / 13% HW pass** | - |

## Geometry (MD169)

240x280 glass inside the ST7789's 240x320 GRAM: **`ROW_Pre = 20`** (the
glass covers GRAM rows 20..299 - verified on the c542 board: 0 shows a
bottom gap, 40 a top gap). MADCTL 0x00, COLMOD 0x55, `INVON`.

## What it does

The md169-style dual-bus demo - the **same pattern set runs on BOTH drive
methods** in one loop:

1. Big-font banner **"now will do / soft SPI test"** (yellow on blue, 3 s),
   then the full pattern set on the **bit-banged** bus.
2. Big-font banner **"now will do / HW SPI1 test"** (black on cyan, 3 s),
   then the same pattern set on **SPI1 @ 25 MHz**.

Pattern set (per pass, live FPS counter throughout): **info page** (normal +
inverted: compiler, build date, freq, active bus + clock, IO map, backlight
duty, UID), vendor **TEST_STAND** (frame / 16-level gray / bands / solid
colors), **HSV gradient** sweep (1 s per wheel), **LED test**.

Note: the full-window fills (clears, TEST_STAND solids) are 240x280 =
67200 pixels - ~4x the 128x128 panel - so the bit-banged pass is
correspondingly slower (single-digit fps on the gradient) while the HW SPI1
pass stays smooth (~20 fps on the gradient).

## Backlight PWM

Same as the baseline: **TIM4_CH4 on PB9 (AF2)** at 1 kHz; **15 % duty during
the soft pass, 13 % during the HW pass** (re-set by `main()` per phase).

## Build / flash / console

```bash
cd app/st7789_md169_240x280
bash build.sh          # == mkdir build && cd build && cmake -G Ninja .. && ninja
ninja flash            # probe-rs download + reset over ST-Link SWD
```

Console is the board's USART1 / ST-Link VCP (COM9, 115200 8-N-1); a banner
prints once at boot and the pattern phases log as they run, looping forever.

> **"Target voltage (VAPP) is 0.02 V" warning** during `ninja flash` can be
> ignored - it is just the ST-Link's target-voltage sensing reporting an
> unreliable reading. Flashing, reset and the VCP console all work fine
> (verified on hardware).

## Hardware SPI (md130/md169-style dual-bus)

Same as the baseline: SPI1 AF5 on PA5 (SCK) / PA7 (MOSI), mode 3, default
prescaler /4 = **25 MHz** (APB2 = 100 MHz via the project's weak
`SystemClock_Config` override; /2 = 50 MHz is the F411 SPI1 max if a unit
proves it can take it). `LCD_UseSoftBus()` / `LCD_UseHwBus()` re-mux
PA5/PA7, then `LCD_Reinit()` re-frames the panel for the freshly selected
bus. HW raster bursts stream through a 512-byte TX buffer.

## Files

- `src/main.c` - md169-style pattern set on both buses: banner, info
  (normal + inverted), vendor TEST_STAND, HSV gradient, LED test, FPS counter
- `src/lcd.c` / `lcd.h` - ST7789V init + 240x280/ROW_Pre=20 geometry +
  drawing API + `LCD_Reinit` (swapped SDA/RES pins)
- `src/lcd/lcd_fonts.c` / `lcd_fonts.h` - ASCII 6x12 font
- `src/lcd/lcd_font_1608.c` / `lcd_font_1608.h` - ASCII 8x16 banner font
- `src/interface.c` / `interface.h` - bus primitives: bit-banged 4-wire SPI
  **and** HW SPI1 (mode 3, buffered raster bursts) + bus switching
- `src/blockwrite/blockwrite.h` - pixel-window helper
- `src/backlight.c` / `backlight.h` - PB9/TIM4_CH4 PWM (+ duty getter)