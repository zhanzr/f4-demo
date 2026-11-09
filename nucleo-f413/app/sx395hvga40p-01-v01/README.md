# sx395hvga40p-01-v01 - ILI9488 3.5" 320x480 LCD on nucleo-f413

Drives the **SX395HVGA40P-01-V01** 3.5" **320x480** module (**ILI9488**
controller, vendor datasheet in the module DB) on the **nucleo-f413**
board (STM32F413ZHT6 @ 100 MHz, its family maximum, from the 16 MHz HSI)
over a 4-wire SPI bus, with two interchangeable drive methods compared
back to back in one demo loop: bit-banged GPIO ("soft SPI") and the SPI1
peripheral ("hardware SPI" @ 12.5 MHz by default; configurable with
`LCD_SPI1_PRESC`).
Ported from `nano-f411/app/st7365_md350_320x480` - the logic is largely
the same (same resolution, same soft+HW dual-bus demo loop, NES pass,
timed solids, FPS band).

Vendor example: `D:\module_db\sx395hvga40p-01-v01\vendor_ex` (F103
std-periph-lib; its default build used the 16-bit parallel bus - this
port uses the module's 4-wire SPI wiring instead).

## Controller notes

- **ILI9488** init (vendor-verbatim): vendor-register unlock
  (0xF7 = A9 51 2C 82), power/timing (0xC0, 0xC1, 0xC5 VCOM, 0xB1 frame
  rate, 0xB4, 0xB6, 0xB7, 0xBE, 0xE9), MADCTL **0x08**, 16-bit COLMOD
  (0x3A = 0x55), positive/negative gamma tables (0xE0/0xE1), sleep out +
  **120 ms**, display on.
- **Geometry**: 320x480, windows at **`COL_Pre = 0`, `ROW_Pre = 0`** (the
  controller addresses the full range directly).
- **Reset**: RST high 50 ms -> low 200 ms -> high + 120 ms, then the init
  sequence (vendor order).
- **Backlight**: PE9, TIM1_CH1 PWM (AF1), 1 kHz at 15% default duty.

## Wiring

| LCD pin | MCU pin | Feature |
| ------- | ------- | ------- |
| SCL | PA5 | SPI clock (soft bit-bang / HW SPI1_SCK AF5) |
| SDA | PA7 | SPI data out (MOSI) |
| RST | **PF12** | Reset (high 50 ms -> low 200 ms -> high, vendor order) |
| DC  | **PD15** | Command/data select (always GPIO) |
| CS  | **PD14** | Chip select (always GPIO) |
| BL  | **PE9** | Backlight - TIM1_CH1 **PWM 1 kHz, 15%** |

### HW SPI1 role of the shared pins

| LCD pin | MCU pin | HW SPI1 (AF5) role |
| ------- | ------- | ------------------ |
| SCL | PA5 | **SPI1_SCK** (hardware) |
| SDA | PA7 | **SPI1_MOSI** (hardware) |
| RST | PF12 | Reset (high 50 ms -> low 200 ms -> high, vendor order) |
| DC  | PD15 | stays GPIO - driven per byte/transfer |
| CS  | PD14 | stays GPIO software CS |

## What it does

The demo loops forever. Each bus (soft, then HW) runs the **full-size
pattern set** followed by a **NES-size pass** confined to a centered
**256x224 window** (`LCD_SetWindow(32, 128, 256, 224)`):

1. Big-font banner **"SX395 320x480 / soft SPI test"** (yellow on blue,
   3 s), then the full pattern set on the **bit-banged** bus.
2. Big-font banner **"SX395 320x480 / HW SPI1 test"** (black on cyan,
   3 s), then the same pattern set on **SPI1 @ 50 MHz**.

Pattern set (per pass, live FPS counter throughout):

1. **TEST_STAND** (500 ms between screens): window-border **frame**,
   **16-level gray** horizontal bars, **color bands**, then full **red,
   green, blue, white, black** fills - each solid fill is **timed**, and

   Measured at the 50 MHz SPI1 rate (460,800 bytes per fill): **~323 ms
   per solid fill** - wire time alone is ~74 ms; the per-byte buffered
   write path is the remaining overhead (a `LCD_FillBulk`-style register
   loop is the next optimization if more speed is needed).
   the durations are reset every pass, so they always belong to the
   current driving method.
2. **Info page** (normal, then **inverted**): compiler, build date, CPU
   frequency, drive method (soft bit-bang / HW SPI1), the IO map, live
   backlight duty, the UID, the panel ID read over MISO at startup, and the measured
   **"xx : xx ms"** solid-fill durations from this pass.
3. **Gradient** - animated HSV hue sweep across the full color wheel
   (4 s per sweep).
4. **LED test** - board LED PC13 on/off.

All with a **live FPS counter** drawn transparently in the bottom band.

### Measured refresh (solid fills): 320x480 vs NES 256x224

| Window | Pixels | Soft bit-bang | HW SPI1 @ 50 MHz |
| ------ | ------ | ------------- | ---------------- |
| 320x480 | 153,600 | ~1164 ms | ~183 ms |
| NES 256x224 | 57,344 (37.5%) | ~436 ms | ~70 ms |

The hardware figures are from the earlier 50 MHz configuration; the
current project defaults to 12.5 MHz. The times scale with the pixel
count on both buses - they are bandwidth/overhead bound, not panel bound.
The soft bit-bang rate is
deliberately NOT tuned beyond ~2.1 MHz: faster variants (direct BSRR
writes, ~7-10 MHz) were tested and leave the panel blank - see the note
in `src/interface.c`. The soft interface pins now run at
`GPIO_SPEED_FREQ_HIGH` (cleanest edges at the proven bit rate).
The soft bit-bang rate is deliberately NOT tuned beyond ~2.1 MHz: faster
variants (direct BSRR writes, ~7-10 MHz) were tested and leave the panel
blank - see the note in `src/interface.c`.

## Backlight PWM

**TIM1_CH1 on PE9 (AF1)** at 1 kHz (`PSC=99`, `ARR=999`, MOE set - TIM1 is
an advanced timer), **15 % duty** default (set at boot;
`Backlight_SetDuty()` rescales CCR1 for 0..100 %,
`Backlight_GetDuty()` reports the live duty shown on the info page).

## Build / flash / console

```bash
cd app/st7365_md350_320x480
bash build.sh          # == mkdir build && cd build && cmake -G Ninja .. && ninja
ninja flash            # probe-rs download + reset over ST-Link SWD
```

Console is the board's USART3 / ST-Link VCP (115200 8-N-1); startup
prints the SPI read diagnostics and the pattern phases log as they run.

> **"Target voltage (VAPP) is 0.02 V" warning** during `ninja flash` can be
> ignored - it is just the ST-Link's target-voltage sensing reporting an
> unreliable reading. Flashing, reset and the VCP console all work fine
> (verified on hardware).

## Hardware SPI (implemented)

- **SPI1 AF5** on PA5 (SCK) / PA7 (MOSI), mode 3 (CPOL=1, CPHA=2EDGE - the
  same idle-high / rising-edge-sampling timing the bit-bang produces),
  8-bit MSB-first, NSS soft. DC/RES/CS stay GPIO.
- **Clock**: APB2 = 100 MHz from the board's F413 clock setup. The current
  default prescaler is **/8 = 12.5 MHz SCK**; define `LCD_SPI1_PRESC` as
  `SPI_BAUDRATEPRESCALER_4` for 25 MHz or
  `SPI_BAUDRATEPRESCALER_2` for 50 MHz. The on-screen info page shows the
  configured rate.
- **Bus switching**: `LCD_UseSoftBus()` / `LCD_UseHwBus()` re-mux PA5/PA7
  (GPIO vs AF5), then `LCD_Reinit()`
  re-frames the panel for the freshly selected bus. HW raster bursts
  stream through a 512-byte TX buffer; inside a burst pairs of bytes go
  out as 16-bit frames (DFF switched to 16-bit only while the SPI is
  disabled, per the reference manual), which halves the per-byte
  DR-write overhead. After each burst the RX echoes are drained and any
  overrun cleared after each burst.
- **Read diagnostics**: startup switches to HW SPI, reinitializes the
  panel, then reads registers `0Ah`-`0Fh` and `RDDID (04h)` over MISO
  (PA6) before printing the results to the USART3 console. Read clocks
  use SPI1 `/32` = 3.125 MHz, below the ILI9488's 150 ns minimum read
  cycle; MOSI is released during the response. `RDDID` has one dummy
  clock before its 24 response bits, so the four captured bytes are
  realigned before reporting the three ID bytes.

## DMA experiment (not enabled; direction documented)

An experimental DMA raster path was implemented register-level and then
**disabled** (build flag `LCD_USE_DMA`, default 0 in `src/interface.c`):
DMA2 Stream5, channel 3 -> SPI1_TX, byte transfers, memory-increment,
armed per 4 KB buffer flush (`spi_hw_tx`).

Observed on hardware: the bursts **complete** (DMA2 HISR TCIF5+HTIF5
verified via SWD) yet the panel shows nothing, while small per-byte
writes (< 32 bytes) render fine. So the bytes are consumed by the SPI
but never reach the panel correctly. Suspects, in order:

- missing FIFO/burst configuration on byte-wide DMA streams;
- SPI CR2 DMAEN handling specifics on the F413;
- the RX side never being drained by DMA (full-duplex overruns) -
  arming the RX stream alongside TX is the standard fix to try first.

This repo previously had **no SPI-DMA usage anywhere** (nano-f411 only
enables the HAL DMA module), so there was no in-repo reference to copy;
the direction above is the documented starting point for resuming.
Current fills run at 323 ms via the CPU-polled per-byte path; wire time
at 50 MHz is ~74 ms, so DMA would gain ~4x once fixed.

## Files

- `src/main.c` - pattern set on both buses: banner, info (normal +
  inverted), TEST_STAND, HSV gradient, LED test, FPS counter
- `src/lcd.c` / `lcd.h` - ILI9488 init + 320x480/COL_Pre=ROW_Pre=0
  geometry + drawing API + `LCD_Reinit`
- `src/lcd/lcd_fonts.c` / `lcd_fonts.h` - ASCII 6x12 font
- `src/lcd/lcd_font_1608.c` / `lcd_font_1608.h` - ASCII 8x16 banner font
- `src/interface.c` / `interface.h` - 4-wire bus primitives: soft
  bit-bang **and** HW SPI1 (mode 3, buffered raster bursts) + bus
  switching
- `src/blockwrite/blockwrite.h` - pixel-window helper
- `src/backlight.c` / `backlight.h` - PE9/TIM1_CH1 PWM (+ duty getter)