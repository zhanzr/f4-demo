# ips395-st7796-captouch - ST7796S 4.0" 320x480 IPS + FT6336U on nucleo-f413

Drives the **CL40BC267-40A** ("40002" 4.0" IPS) module on the
**nucleo-f413** board (STM32F413ZHT6 @ 100 MHz) over a 4-wire SPI bus,
with two interchangeable drive methods compared back to back in one demo
loop: bit-banged GPIO ("soft SPI") and the SPI1 peripheral ("hardware
SPI" @ **50 MHz**, the SPI1 maximum via the F413 native APB2 = 100 MHz
tree; prescaler knob `LCD_SPI1_PRESC`).

Module documentation: `D:\module_db\ips395-st7796-captouch` (vendor
ST7796S init text, ST7796S datasheet, FT6336U datasheet + register map,
STM32F1/STC32 3/4-line-SPI and parallel demos).

## Controller notes

- **ST7796S**, 320x480, IPS. Vendor-verbatim init
  (`IC&Code\ST7796S_BOE3.98_IPS_Init.txt` = the 4SPI demo's `LCD_Init`):
  sleep out + 120 ms, `F0=C3` / `F0=96` (Command2 BK0 unlock), MADCTL,
  COLMOD, `B4=01`, `B7=C6`, `E8=40 8A 00 00 29 19 A5 33`, `C1=06`,
  `C2=A7`, `C5=18`, gamma `E0`/`E1`, `F0=3C` / `F0=69` (BK1), `21`
  (inversion on) + 120 ms, `29` (display on).
- **RGB565 in 4-line serial is supported** - that is the speed win over
  the ILI9488 module next door: COLMOD `0x3A = 0x55` and **two bytes per
  pixel** (MSB first, no per-pixel color conversion). The vendor demo
  writes 16 bits per pixel over the same 4-wire link. A full 320x480
  frame is therefore 307,200 bytes instead of 460,800.
- **MADCTL (`0x36`) is the mirror knob**: vendor portrait = **0x48**
  (MY=0, MX=1, MV=0, RGB=1) - verified correct on this module, and it
  is exactly what the ILI9488 driver's `0x08` was missing (that port
  rendered the panel mirrored). Override at build time without editing
  code: `bash build.sh -DLCD_MADCTL=0x88` (any hex; re-run cmake).
  Reference:
  `0x08` none, `0x48` MX, `0x88` MX+MY (180 deg), `0xC8` MY,
  `0x28` MV (landscape), `0xE8` MV+MX+MY. Clear bit 3 (RGB) if red and
  blue come out swapped.
- **Reset**: RST high 50 ms -> low 200 ms -> high + 120 ms (the vendor's
  init then starts with sleep out).
- **Backlight**: PE9, TIM1_CH1 PWM (AF1), 1 kHz. The module drives the
  LED pin through a transistor ("default off, high = on"), so duty cycle
  maps to brightness directly.

## Wiring (display)

The module's 14-pin header carries the 4-line SPI interface plus the
capacitive-touch pins on 10..14:

| Module pin (J1) | MCU pin | Feature | HW SPI1 role |
| --------------- | ------- | ------- | ------------ |
| 1 VCC | 3V3 | +3.3 V | |
| 2 GND | GND | ground | |
| 3 SPI_CS | **PD14** | chip select | stays GPIO |
| 4 SPI_RESET | **PF12** | panel reset (low active) | stays GPIO |
| 5 SPI_DC | **PD15** | command/data select | stays GPIO |
| 6 SPI_SDA | PA7 | data in (MOSI) | SPI1_MOSI (AF5) |
| 7 SPI_SCK | PA5 | clock | SPI1_SCK (AF5) |
| 8 LED | **PE9** | backlight (high = on) | TIM1_CH1 PWM |
| 9 SPI_SDO | PA6 | data out (MISO) | SPI1_MISO (AF5), unused |
| 10 TP_SCK | (reserved) | touch I2C clock | see below |
| 11 TP_CS | (reserved) | touch reset on this module | see below |
| 12 TP_SDI | (reserved) | touch I2C data | see below |
| 13 TP_SDO | - | not connected on the CTP build | |
| 14 TP_IRQ | (reserved) | touch interrupt | see below |

The dedicated 6-pin touch header **J4** (`VCC SDA SCL INT RST GND`) is
the cleaner place to pick the I2C signals up; the module already fits
10K pull-ups on SDA/SCL there (R14/R15).

## Touch: reserved pin plan (not implemented yet)

| Signal | Planned MCU pin | Peripheral | Note |
| ------ | --------------- | ---------- | ---- |
| T_INT | **PG2** | GPIO input, EXTI2 | free on LQFP144 |
| T_RST | **PG3** | GPIO output | low active, 20 ms + 300 ms settle |
| T_SCL | **PB8** | I2C1_SCL (AF4) | hardware I2C available |
| T_SDA | **PB9** | I2C1_SDA (AF4) | hardware I2C available |

Feasibility check against this board and the rest of this project:

- PB8/PB9 are exactly **I2C1 SCL/SDA on AF4** on the STM32F4 family, so
  the pair gives a hardware I2C master (no bit-bang needed). APB1 runs
  at 50 MHz here, comfortably inside I2C1's range; FT6336U tops out at
  400 kHz (fast mode, 100 kHz is a safe start).
- PG2/PG3 are plain GPIO on the 144-pin package. Nothing else in this
  project or the board layer uses them (LEDs PB0/PB7/PB14, button PC13,
  console USART3 PD8/PD9, display PA5/PA6/PA7/PD14/PD15/PF12/PE9), and
  no other pin on EXTI line 2 is in use, so PG2 can interrupt freely.
- The module already carries the I2C pull-ups; nothing extra is needed.
  FT6336U answers at 7-bit **0x38** (write 0x70 / read 0x71).
- Nothing on PG2/PG3/PB8/PB9 conflicts with SPI1 (PB8/PB9 are also
  TIM4_CH3/CH4 and I2C1 - both unused here).

Verdict: the plan is **electrically and peripherally sound** - no
conflicts, one hardware I2C available, one free EXTI line. Two things to
watch when wiring: keep the I2C lines short (10K pull-ups are weak-ish
for long leads) and do not add internal pull-ups on top of the module's
(prefer `GPIO_NOPULL` + AF4 open-drain).

## Demo loop

The demo loops forever, soft pass then HW pass, each running the full
320x480 pattern set:

1. Big-font banner **"IPS395 320x480 / SOFT SPI BL 15%"** (yellow on
   blue), then the pattern set on the bit-banged bus.
2. Big-font banner **"IPS395 320x480 / HW 50MHz RGB565 BL 30%"** (black
   on cyan), then the same set on **SPI1 @ 50 MHz**.

Pattern set (live FPS counter throughout):

1. **TEST_STAND** (500 ms per screen): red frame, 16-level gray bars,
   color bands, then full red / green / blue / white / black fills -
   each solid fill is **timed** and **labeled on-panel**, and the
   durations are reset every pass so they always describe the current
   drive method.
2. **Info page** (normal, then inverted): compiler, build date, CPU
   frequency, drive method + pixel format, the IO map, backlight duty,
   the UID and the measured fill times. Both pages are painted by the
   single-pass renderer (`LCD_DrawTextPage`): background and every text
   line in one continuous raster stream.
3. **Gradient** - animated HSV hue sweep (4 s per sweep).
4. **LED test** - board LED LD1 (PB0) on/off.

## Measured

Verified on hardware: portrait orientation and colors correct at
MADCTL `0x48`, RGB565, 50 MHz SPI1 - no garbling on either bus.

At 50 MHz SPI1, 307,200 bytes per full frame (wire floor 49 ms):

- HW pass: **51-52 ms per solid fill** - essentially the wire time; the
  fills are streamed by DMA from a prepared color pattern, so no
  per-byte CPU work is left.
- SOFT pass: **767 ms per solid fill** (bit-bang, unchanged in method).
- Same 18-bit RGB666 fills on the ILI9488 module: 76 ms / 1147 ms - the
  16-bit frame is ~1.5x cheaper on both buses.

## DMA raster path (DMA2 Stream5 ch3 -> SPI1_TX)

Raster bytes are rendered into **one of two 4 KB buffers** and streamed
by DMA, so byte generation and the wire overlap; solid fills skip
generation entirely (`LCD_StreamColor16`: a 1024-pixel color pattern
re-armed as repeated bursts). Raw register-level configuration: byte
transfers, memory-increment, peripheral fixed, direct mode (no FIFO),
normal mode, very high priority, polled to completion, no NVIC. Commands
and short tails (< 32 bytes) stay on the polled per-byte path.

Verified on the sister module and reused here: a burst runs at exactly
16 core cycles/byte (8 bits x 20 ns at 50 MHz), so every byte is on the
wire; 2-line full duplex needs no RX drain (overrun does not stall TX).

## Not implemented yet

- **Touch (FT6336U)**: pins reserved as above; driver comes next.
- **Panel reads (RDDID)**: the module routes SPI_SDO to the header, so
  reading the controller ID is possible later - the ST7796S form is one
  dummy byte after the command (the vendor demo's `LCD_ReadPoint` shows
  the 16-clock read pattern). Not used by this driver.

## Files

- `src/main.c` - pattern set on both buses: banner, info (normal +
  inverted), TEST_STAND (timed solids), gradient, LED test
- `src/interface.c` / `interface.h` - soft bit-bang + HW SPI1 dual-bus
  primitives, double-buffered SPI1_TX DMA bursts, 16-bit color streamer
- `src/lcd.c` / `lcd.h` - ST7796S init + 320x480 geometry + drawing API
  (RGB565 pixel writer, single-pass page renderer, `LCD_MADCTL` knob)
- `src/backlight.c` / `backlight.h` - PE9/TIM1_CH1 PWM (+ duty getter)
- `src/lcd/lcd_fonts.c` / `lcd_fonts.h` - ASCII 6x12 font
- `src/lcd/lcd_font_1608.c` / `lcd_font_1608.h` - ASCII 8x16 banner font
- `src/blockwrite/blockwrite.h` - pixel-window helper

## Build / flash

```bash
bash build.sh                  # CMake + Ninja
ninja flash                    # probe-rs over SWD (probe pinned in cmake)
ninja flash-ocd                # alternative: OpenOCD
```

Console: **USART3 PD8/PD9**, 115200 8-N-1 (ST-LINK VCP).
