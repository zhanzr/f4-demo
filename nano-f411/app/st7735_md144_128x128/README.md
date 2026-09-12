# st7735_md144_128x128 - ST7735S 1.44" 128x128 LCD on nano-f411

Drives an ST7735S 1.44" 128x128 (**MD144**) module on the **nano-f411** board
(STM32F411CEU6 @ 100 MHz), ported from the vendor example
`C8T6_md144_t1` (bit-banged 4-wire SPI).
(Renamed from `st7735_test` to match the repo's `<panel>_<module>_<res>`
naming.)

## Wiring (single table: pin, feature, both drive methods)

**SDA and RES are swapped** vs the vendor example (SDA PA6 -> PA7,
RES PA7 -> PA6). One wiring serves both drive methods:

| LCD pin | MCU pin | Feature | HW SPI1 (AF5) role |
| ------- | ------- | ------- | ------------------ |
| SCL | PA5 | SPI clock | **SPI1_SCK** (hardware) |
| SDA | **PA7** | SPI MOSI - data out (vendor used PA6) | **SPI1_MOSI** (hardware) |
| RES | **PA6** | Reset (vendor used PA7) | stays GPIO (SPI1_MISO unused - write-only panel) |
| DC  | PA4 | Data/command select | stays GPIO (SPI1_NSS unused), driven per byte/transfer |
| CS  | PB8 | Chip select | stays GPIO software CS (PB8 has no SPI1 AF) |
| BL  | PB9 | Backlight - **TIM4_CH4 PWM: 15% soft pass / 13% HW pass** | - |

## What it does

Test patterns ported from the c542 `st7789_md130_240x240` demo
(`bsp/st7789/main.c`), adapted to 128x128 - and like md130, the **same
pattern set runs on BOTH drive methods** in one loop:

1. Big-font banner **"now will do / soft SPI test"** (yellow on blue, 3 s),
   then the full pattern set on the **bit-banged** bus.
2. Big-font banner **"now will do / HW SPI1 test"** (black on cyan, 3 s),
   then the same pattern set on **SPI1 hardware**.

Pattern set (per pass, live FPS counter throughout):

1. **Info page** (normal, then **inverted**): compiler, build date, CPU
   frequency, active drive method (soft bit-bang / HW SPI1 + clock), the
   swapped IO map, live backlight duty (15%/13%) and the UID (reformatted to
   the 128 px width; the md130 ADC block is dropped because this board's
   baseline has no `adc_internal` helper).
2. **Vendor `TEST_STAND`** (500 ms between screens): window-border **frame**,
   **16-level gray** horizontal bars, **color bands**, then full **red,
   green, blue, white, black** fills. The init sequence (frame rate / power /
   gamma / MADCTL `0xC8`, 65k mode) matches the vendor
   `001_006_ST7735S_1.44_0xC8.h` verbatim.
3. **Gradient** - animated HSV hue sweep across the full color wheel
   (1 s per sweep, one full wheel per pass).
4. **LED test** - board LED PC13 on/off.

(The shapes / pure-color demos of the old `st7735_test` are dropped to match
md130, which removed them as redundant: `TEST_STAND`'s `DispColor` already
covers the solid colors and the gradient/CLS paths exercise the primitives.
The FPS counter makes the bus difference obvious: single-digit fps on the
gradient over bit-bang vs well over 100 fps on HW SPI1.)

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
`ARR=999`, init `CCR4=150` (**15 %** - the demo's soft-pass duty). APB1 =
50 MHz, timer clock x2 = 100 MHz. `main()` re-sets the duty per pass -
**15 % during the soft (bit-bang) pass, 13 % during the HW SPI1 pass**.
`Backlight_SetDuty()` rescales CCR for 0..100 %; `Backlight_GetDuty()`
reports the live duty (shown on the info page).

## Build / flash / console

```bash
cd app/st7735_md144_128x128
bash build.sh          # == mkdir build && cd build && cmake -G Ninja .. && ninja
ninja flash            # probe-rs download + reset over ST-Link SWD
```

Console is the board's USART1 / ST-Link VCP (COM9, 115200 8-N-1); a banner
prints once at boot and the pattern phases log as they run, looping forever.
(The demo now runs the SOFT pass then the HARDWARE pass, so the phase log
alternates between "on SOFT SPI" and "on HARDWARE SPI1".)

> **"Target voltage (VAPP) is 0.02 V" warning** during `ninja flash` can be
> ignored - it is just the ST-Link's target-voltage sensing reporting an
> unreliable reading. Flashing, reset and the VCP console all work fine
> (verified on hardware).

## Hardware SPI (implemented, md130-style dual-bus)

The swapped wiring maps directly onto the **SPI1** peripheral (AF5) - the
SDA/RES swap actually *aligned* the wiring with SPI1's default pins (before
the swap SDA=PA6 was SPI1_MISO, useless for a write-only panel). See the
single wiring table above for the per-pin HW roles.

- **Timing**: SPI mode 3 (CPOL=1, CPHA=1) replicates the bit-bang's
  idle-high / rising-edge-sampling timing exactly.
- **Clock**: SPI1 is clocked from APB2. This project overrides the weak
  `SystemClock_Config()` to run **APB2 at 100 MHz** (the F411's PCLK2 max;
  the board default tree divides it to 50 MHz). The default prescaler is
  **/4 = 25 MHz SCK** - fast yet the closest to the ST7735S ~15 MHz
  write-cycle spec that stayed stable. The board's absolute fastest,
  **/2 = 50 MHz** (the F411 datasheet's SPI1 max), is available via
  `LCD_SPI1_PRESC=SPI_BAUDRATEPRESCALER_2` but outruns the panel spec;
  **/8 = 12.5 MHz** is the conservative fallback. The core stays at
  100 MHz and USART1 recomputes its baud from the live PCLK2, so the
  console stays at 115200 (verified).
- **Bus switching**: `LCD_UseSoftBus()` / `LCD_UseHwBus()` re-mux PA5/PA7
  (GPIO vs AF5), then `LCD_Reinit()` (reset + init sequence) re-frames the
  panel for the freshly selected bus - the same flow as md130. HW raster
  bursts stream through a 512-byte TX buffer (one `HAL_SPI_Transmit` per
  <=512 bytes); commands stay unbuffered single-byte transmits.
- **Caveat**: module-dependent - the c542 MD130 module's HW SPI was
  physically broken (see its README); this MD144's HW path passes the full
  pattern set (verified at 12.5 MHz; 50 MHz is 3x the ST7735S write-cycle
  spec, so check the HW pass visually and use the prescaler knob if needed).

## Files

- `src/main.c` - md130-style pattern set, run on both buses: banner, info
  (normal + inverted), vendor TEST_STAND, HSV gradient, LED test, FPS counter
- `src/lcd.c` / `lcd.h` - driver + vendor demo screens + 24-bit color API,
  text, lines/rects/circles/fills, `LCD_CopyBuffer`, `LCD_Reinit`
  (swapped SDA/RES pins)
- `src/lcd/lcd_fonts.c` / `lcd_fonts.h` - ASCII 6x12 font
- `src/lcd/lcd_font_1608.c` / `lcd_font_1608.h` - ASCII 8x16 banner font
- `src/interface.c` / `interface.h` - bus primitives: bit-banged 4-wire SPI
  **and** HW SPI1 (mode 3, buffered raster bursts) + bus switching
- `src/blockwrite/blockwrite.h` - pixel-window helper
- `src/backlight.c` / `backlight.h` - PB9/TIM4_CH4 PWM (+ duty getter)