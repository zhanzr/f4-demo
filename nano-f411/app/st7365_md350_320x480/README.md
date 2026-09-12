# st7365_md350_320x480 - ST7365P 3.5" 320x480 LCD on nano-f411

Drives the **MD350** 3.5" **320x480** module (**ST7365P** controller,
ST7796-register-compatible) on the **nano-f411** board (STM32F411CEU6 @
100 MHz) over a 4-wire SPI bus, with two interchangeable drive methods
compared back to back in one demo loop: bit-banged GPIO ("soft SPI") and
the SPI1 peripheral ("hardware SPI" @ 50 MHz).

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
| MISO | PA6 | Module read-back line - used for the panel **IC ID** read (HW bus; reads are not supported by the soft bit-bang) |

### HW SPI1 role of the shared pins

| LCD pin | MCU pin | HW SPI1 (AF5) role |
| ------- | ------- | ------------------ |
| SCL | PA5 | **SPI1_SCK** (hardware) |
| SDA | PA7 | **SPI1_MOSI** (hardware) |
| RES | PA3 | stays GPIO |
| DC  | PA4 | stays GPIO - driven per byte/transfer |
| CS  | PB8 | stays GPIO software CS |
| MISO | PA6 | maps to **SPI1_MISO** (AF5) - unused by this TX-only driver |

## What it does

The demo loops forever, running the **same pattern set on BOTH drive
methods**:

1. Big-font banner **"MD350 320x480 / soft SPI test"** (yellow on blue,
   3 s), then the full pattern set on the **bit-banged** bus.
2. Big-font banner **"MD350 320x480 / HW SPI1 test"** (black on cyan,
   3 s), then the same pattern set on **SPI1 @ 50 MHz**.

Pattern set (per pass, live FPS counter throughout):

1. **TEST_STAND** (500 ms between screens): window-border **frame**,
   **16-level gray** horizontal bars, **color bands**, then full **red,
   green, blue, white, black** fills - each solid fill is **timed**, and
   the durations are reset every pass, so they always belong to the
   current driving method.
2. **Info page** (normal, then **inverted**): compiler, build date, CPU
   frequency, drive method (soft bit-bang / HW SPI1), the IO map, live
   backlight duty, the UID, the panel **IC ID** read over MISO (0xD3:
   `1D E5 80` - the read works on the HW bus; the soft bit-bang does not
   meet the panel's read timing, so soft shows "ID --"), and the measured
   **"xx : xx ms"** solid-fill durations from this pass.
3. **Gradient** - animated HSV hue sweep across the full color wheel
   (4 s per sweep).
4. **LED test** - board LED PC13 on/off.

All with a **live FPS counter** drawn transparently in the bottom band.
Measured fills: soft ~1160 ms per solid fill vs ~183 ms on HW SPI1 @ 50
MHz (the wire floor at 50 MHz is ~50 ms; the HW path is ~4.5x faster than
the soft pass even though the per-byte polling overhead dominates both).
The soft bit-bang rate is deliberately NOT tuned beyond ~2.1 MHz: faster
variants (direct BSRR writes, ~7-10 MHz) were tested and leave the panel
blank - see the note in `src/interface.c`.

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

## Hardware SPI (implemented)

- **SPI1 AF5** on PA5 (SCK) / PA7 (MOSI), mode 3 (CPOL=1, CPHA=1 - the
  same idle-high / rising-edge-sampling timing the bit-bang produces),
  8-bit MSB-first, NSS soft. DC/RES/CS stay GPIO.
- **Clock**: APB2 = 100 MHz (this project overrides the weak
  `SystemClock_Config()` to run PCLK2 at the F411 max), default prescaler
  **/2 = 50 MHz SCK** (the F411 SPI1 max, first-try setting per the
  module's behavior); `LCD_SPI1_PRESC=SPI_BAUDRATEPRESCALER_4` drops it
  to 25 MHz if a unit needs a slower rate. The core stays at 100 MHz and
  USART1 recomputes its baud from the live PCLK2, so the console stays at
  115200. The on-screen info page shows the active rate as an integer
  ("SPI1 50 MHz (HW)").
- **Bus switching**: `LCD_UseSoftBus()` / `LCD_UseHwBus()` re-mux PA5/PA7
  (GPIO vs AF5; PA6 = MISO also muxed on HW), then `LCD_Reinit()`
  re-frames the panel for the freshly selected bus. HW raster bursts
  stream through a 512-byte TX buffer; inside a burst pairs of bytes go
  out as 16-bit frames (DFF switched to 16-bit only while the SPI is
  disabled, per the reference manual), which halves the per-byte
  DR-write overhead. After each burst the RX echoes are drained and any
  overrun cleared, keeping the line clean for MISO read-back.

## Files

- `src/main.c` - pattern set on both buses: banner, info (normal +
  inverted), TEST_STAND, HSV gradient, LED test, FPS counter
- `src/lcd.c` / `lcd.h` - ST7365P init + 320x480/COL_Pre=ROW_Pre=0
  geometry + drawing API + `LCD_Reinit`
- `src/lcd/lcd_fonts.c` / `lcd_fonts.h` - ASCII 6x12 font
- `src/lcd/lcd_font_1608.c` / `lcd_font_1608.h` - ASCII 8x16 banner font
- `src/interface.c` / `interface.h` - 4-wire bus primitives: soft
  bit-bang **and** HW SPI1 (mode 3, buffered raster bursts) + bus
  switching
- `src/blockwrite/blockwrite.h` - pixel-window helper
- `src/backlight.c` / `backlight.h` - PB9/TIM4_CH4 PWM (+ duty getter)