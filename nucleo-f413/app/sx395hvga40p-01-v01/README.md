# sx395hvga40p-01-v01 - ILI9488 3.5" 320x480 LCD on nucleo-f413

Drives the **SX395HVGA40P-01-V01** 3.5" **320x480** module (**ILI9488**
controller, vendor datasheet in the module DB) on the **nucleo-f413**
board (STM32F413ZHT6 @ 100 MHz, its family maximum, from the 16 MHz HSI)
over a 4-wire SPI bus, with two interchangeable drive methods compared
back to back in one demo loop: bit-banged GPIO ("soft SPI", ~2.8 Mbps)
and the SPI1 peripheral ("hardware SPI" @ **50 MHz**, the SPI1 maximum
via the F413 native APB2=100 MHz tree; prescaler knob `LCD_SPI1_PRESC`).
Ported from `nano-f411/app/st7365_md350_320x480` - the logic is largely
the same (same resolution, same soft+HW dual-bus demo loop, timed
solids, FPS band).

Vendor example: `D:\module_db\sx395hvga40p-01-v01\vendor_ex` (F103
std-periph-lib; its default build used the 16-bit parallel bus - this
port uses the module's 4-wire SPI wiring instead).

## Controller notes

- **ILI9488** init (vendor-verbatim): vendor-register unlock
  (0xF7 = A9 51 2C 82), power/timing (0xC0, 0xC1, 0xC5 VCOM, 0xB1 frame
  rate, 0xB4, 0xB6, 0xB7, 0xBE, 0xE9), MADCTL **0x08**, **18-bit COLMOD
  (0x3A = 0x66)** - REQUIRED in serial mode: the F103 vendor example's
  0x55/16-bit value is only valid on its parallel bus, and 2-byte pixels
  in serial mode garble everything -, positive/negative gamma tables
  (0xE0/0xE1), sleep out + **120 ms**, display on.
- **Geometry**: 320x480, windows at **`COL_Pre = 0`, `ROW_Pre = 0`** (the
  controller addresses the full range directly).
- **Reset**: RST high 50 ms -> low 200 ms -> high + 120 ms, then the init
  sequence (vendor order).
- **Backlight**: PE9, TIM1_CH1 PWM (AF1), 1 kHz.

## Wiring

| LCD pin | MCU pin | Feature | HW SPI1 role |
| ------- | ------- | ------- | ------------ |
| SCL | PA5 | SPI clock (soft bit-bang / HW) | SPI1_SCK (AF5, hardware) |
| SDA/MOSI | PA7 | SPI data out | SPI1_MOSI (AF5, hardware) |
| CS | **PD14** | Chip select (always GPIO) | stays GPIO software CS |
| DC | **PD15** | Command/data select (always GPIO) | stays GPIO, driven per transfer |
| RST | **PF12** | Reset (high 50 ms -> low 200 ms -> high, vendor order) | stays GPIO |
| BL | **PE9** | Backlight - TIM1_CH1 **PWM 1 kHz** | stays GPIO |
| MISO | PA6 | Panel SDO - wired for diagnostics; **reads are not supported in 4-line serial mode** (all reads return 0x00) | SPI1_MISO (AF5), unused |

## Demo loop

The demo loops forever. Each bus (soft, then HW) runs the **full-size
pattern set**:

1. Big-font banner **"SX395 320x480 / soft SPI test"** (yellow on blue,
   3 s), then the full pattern set on the **bit-banged** bus.
2. Big-font banner **"SX395 320x480 / HW SPI1 test"** (black on cyan),
   then the same pattern set on **SPI1 @ 50 MHz**.

Pattern set (per pass, live FPS counter throughout):

1. **TEST_STAND** (500 ms between screens): window-border **frame**,
   **16-level gray** horizontal bars, **color bands**, then full **red,
   green, blue, white, black** fills - each solid fill is **timed** and
   **labeled on-panel**, and the durations are reset every pass, so they
   always belong to the current driving method.

   Measured at the 50 MHz SPI1 rate (460,800 bytes per fill): **~76 ms
   per solid fill**, i.e. the wire time (460,800 x 8 bits / 50 MHz =
   74 ms) nearly exactly - the fills are streamed by DMA from a prepared
   color pattern, so no per-byte CPU work is left in the path. The soft
   pass takes ~1147 ms per fill (bit-bang, unchanged in method).
2. **Info page** (normal, then **inverted**): compiler, build date, CPU
   frequency, drive method (soft bit-bang / HW SPI1), the IO map, live
   backlight duty, the UID, and the measured **"xx : xx ms"**
   solid-fill durations from this pass. All page text is drawn with a
   **single-pass page renderer** (`LCD_DrawTextPage`) that paints the
   background and every text line into one continuous raster stream.
3. **Gradient** - animated HSV hue sweep across the full color wheel
   (4 s per sweep).
4. **LED test** - board LED LD1 (PB0) on/off.

All with a **live FPS counter** drawn transparently in the bottom band.

## Backlight PWM

**TIM1_CH1 on PE9 (AF1)** at 1 kHz (`PSC=99`, `ARR=999`, MOE set - TIM1 is
an advanced timer), default **15 % duty** on the soft pass and **30 %**
on the HW pass (`Backlight_SetDuty()` rescales CCR1 for 0..100 %,
`Backlight_GetDuty()` reports the live duty shown on the info page).

## DMA raster path (DMA2 Stream5 ch3 -> SPI1_TX)

Raster bytes are rendered into **one of two 4 KB buffers** and
streamed by DMA, so byte generation and the wire overlap;
solid fills skip generation altogether (`LCD_StreamColor565`: a 1024-pixel
color pattern re-armed as repeated bursts). Configuration is raw
register-level - byte transfers, memory-increment, peripheral fixed,
direct mode (no FIFO), normal mode, very high priority, polled to
completion, no NVIC. Commands and short tails (< 32 bytes) stay on the
polled per-byte path.

### Why the earlier attempt appeared to fail

Both earlier variants were **false negatives**, not a DMA hardware
problem:

- the HAL-managed path (`HAL_SPI_Transmit_DMA`) returned **HAL_BUSY** and
  the return value was ignored: `HAL_DMA_Start_IT()` takes the handle
  lock and sets `State = BUSY`, and both are only released by the DMA
  transfer-complete *interrupt* - which was never enabled. Every burst
  after the first silently transmitted nothing, while the code polled
  NDTR (0 from the previous burst) and "completed" instantly;
- the register-level debug helper framed no CS (it never pulled CS low),
  so its bytes were clocked out on a deselected panel and dropped.

Measured with DMA now: a 9600-byte burst takes **153,607 core cycles =
16 cycles/byte** - exactly 8 bits x 20 ns at 50 MHz, so every byte is
really on the wire. A stalled burst would finish in a few cycles
regardless of length, which is the quick way to tell the two apart.

### Facts verified on the panel

- Stream5/ch3 (the standard F4 SPI1_TX mapping) is correct; Stream3/ch3
  is not usable (partial transfers only).
- 2-line full duplex with the RX side left undrained works: the overrun
  flag does **not** stall TX, so no RX stream/drain is needed during a
  burst. 1-line (half-duplex) also works but is not required.
- 25 MHz and 50 MHz both render clean; 12.5 MHz garbles (panel clock
  divider window) - the same rate caveat as the polled path.

## Files

- `src/main.c` - pattern set on both buses: banner, info (normal +
  inverted), TEST_STAND (timed solids), gradient, LED test
- `src/interface.c` / `interface.h` - soft bit-bang + HW SPI1 dual-bus
  primitives, double-buffered SPI1_TX DMA bursts, color-pattern streamer
- `src/lcd.c` / `lcd.h` - ILI9488 init + 320x480 geometry + drawing API
  (RGB666 pixel writer, single-pass page renderer) + `LCD_Reinit`
- `src/backlight.c` / `backlight.h` - PE9/TIM1_CH1 PWM (+ duty getter)
- `src/lcd/lcd_fonts.c` / `lcd_fonts.h` - ASCII 6x12 font
- `src/lcd/lcd_font_1608.c` / `lcd_font_1608.h` - ASCII 8x16 banner font
- `src/blockwrite/blockwrite.h` - pixel-window helper
