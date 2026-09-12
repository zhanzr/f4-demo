# st7735_md144_128x128 - ST7735S 1.44" 128x128 LCD on nano-f411

Drives an ST7735S 1.44" 128x128 (**MD144**) module on the **nano-f411** board
(STM32F411CEU6 @ 100 MHz), ported from the vendor example
`C8T6_md144_t1` (bit-banged 4-wire SPI), with the backlight PWM'd at ~20 %.
(Renamed from `st7735_test` to match the repo's `<panel>_<module>_<res>`
naming.)

## Wiring

**SDA and RES are swapped** vs the vendor example (SDA PA6 -> PA7,
RES PA7 -> PA6):

| LCD pin | MCU pin | Role |
| ------- | ------- | ---- |
| SCL | PA5 | SPI clock (bit-banged) |
| SDA | **PA7** | SPI MOSI (bit-banged; vendor used PA6) |
| RES | **PA6** | Reset (vendor used PA7) |
| DC  | PA4 | Data/command |
| CS  | PB8 | Chip select |
| BL  | PB9 | Backlight - **TIM4_CH4, ~20% PWM** |

## What it does

Test patterns ported from the c542 `st7789_md130_240x240` demo
(`bsp/st7789/main.c`), adapted to 128x128, looped forever:

1. **Big-font banner** (8x16 font): "MD144 128x128 / soft SPI test" (3 s).
2. **Info page** (normal, then **inverted**): compiler, build date, CPU
   frequency, drive method (soft bit-bang), the full swapped IO map,
   live backlight duty, and the 96-bit UID (reformatted to the 128 px width -
   max 21 chars/line with the 6x12 font; the ADC block of the md130 demo is
   dropped because this board's baseline has no `adc_internal` helper).
3. **Vendor `TEST_STAND`** (500 ms between screens): window-border **frame**,
   **16-level gray** horizontal bars, **color bands**, then full **red,
   green, blue, white, black** fills. The init sequence (frame rate / power /
   gamma / MADCTL `0xC8`, 65k mode) matches the vendor
   `001_006_ST7735S_1.44_0xC8.h` verbatim.
4. **Gradient** - animated HSV hue sweep across the full color wheel (~2 s).
5. **LED test** - board LED PC13 on/off.

All with a **live FPS counter** drawn transparently in the bottom band.
(The shapes / pure-color demos of the old `st7735_test` are dropped to match
md130, which removed them as redundant: `TEST_STAND`'s `DispColor` already
covers the solid colors and the gradient/CLS paths exercise the primitives.)

The F411 runs at 100 MHz (vs the vendor's 72 MHz F103); the FPS counter shows
the real throughput of the bit-banged bus (~a few frames/s on the gradient).

## DisplayChar fixes

Two `LCD_DisplayChar` bugs in `src/lcd.c` (both latent - the old demo only
ever drew the transparent FPS text):

1. **Glyph buffer too small**: the fixed `Buff[6 * 12]` (72 entries) overflowed
   the stack with the 8x16 banner font (128 pixels) and hung the firmware.
   The buffer is now `Buff[8 * 16]` (largest font).
2. **Garbled opaque text**: the opaque path filled the buffer *linearly from
   the raw glyph bit stream*, but `LCD_CopyBuffer` writes it *row-major* to
   the panel. The 6x12 font packs each 6-pixel row into one byte (2 padding
   bits), so the padding bled into the next row and smeared every glyph -
   that was the info-page garble (the transparent FPS text uses a per-row
   mapping and was always fine). The opaque path now uses the same per-row
   mapping as the transparent path (`bytesPerRow = Sizes / Height`,
   bit 0 = leftmost).

## Backlight PWM

`src/backlight.c` sets up **TIM4_CH4 on PB9 (AF2)** at 1 kHz: `PSC=99`,
`ARR=999`, `CCR4=200` - **~20 % duty**. APB1 = 50 MHz, timer clock x2 =
100 MHz. `Backlight_SetDuty()` rescales CCR for 0..100 %;
`Backlight_GetDuty()` reports the live duty (shown on the info page).

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

## Hardware SPI feasibility (investigation)

**Yes - the current (swapped) wiring is directly usable by the SPI1
peripheral** (AF5). The SDA/RES swap actually *aligned* the wiring with
SPI1's default pins (before the swap SDA=PA6 was SPI1_MISO, useless for a
write-only panel):

| LCD pin | MCU pin | SPI1 (AF5) role | Notes |
| ------- | ------- | --------------- | ----- |
| SCL | PA5 | **SPI1_SCK** | hardware clock |
| SDA | PA7 | **SPI1_MOSI** | hardware data out |
| RES | PA6 | (SPI1_MISO unused) | stays GPIO - write-only panel needs no MISO |
| DC  | PA4 | (SPI1_NSS unused) | stays GPIO - driven per byte/transfer |
| CS  | PB8 | - | stays GPIO software CS (PB8 has no SPI1 AF) |

- **Timing**: the bit-bang idles SCL high and samples SDA on the rising edge
  = SPI **mode 3** (CPOL=1, CPHA=1); hardware SPI in mode 3 replicates the
  proven timing exactly.
- **Speed**: SPI1 is clocked from APB2 = 50 MHz on this board, so the fastest
  baud is **25 MHz** (prescaler /2) - roughly 100x the bit-banged throughput;
  start conservative (e.g. 6-12 MHz, prescaler /8../4) and raise if stable.
- **Caveat**: module-dependent - the c542 MD130 module's HW SPI was
  physically broken (see its README); the MD144 needs a real test. A natural
  follow-up is the md130-style SOFT/HW bus compare in one demo loop, plus DMA
  for the big fills (CopyBuffer/gradient).

## Files

- `src/main.c` - md130-style pattern set: banner, info (normal + inverted),
  vendor TEST_STAND, HSV gradient, LED test, FPS counter
- `src/lcd.c` / `lcd.h` - driver + vendor demo screens + 24-bit color API,
  text, lines/rects/circles/fills and `LCD_CopyBuffer` (swapped SDA/RES pins)
- `src/lcd/lcd_fonts.c` / `lcd_fonts.h` - ASCII 6x12 font
- `src/lcd/lcd_font_1608.c` / `lcd_font_1608.h` - ASCII 8x16 banner font
- `src/interface.c` / `interface.h` - bit-banged 4-wire SPI primitives
  (+ fast raster burst helpers)
- `src/blockwrite/blockwrite.h` - pixel-window helper
- `src/backlight.c` / `backlight.h` - PB9/TIM4_CH4 PWM (+ duty getter)