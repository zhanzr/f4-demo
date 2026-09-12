# nv3030b_md183_240x284_cst816d - NV3030B 1.83" 240x284 LCD + CST816D touch

Drives the **MD183** 1.83" **240x284** module — **NV3030B** LCD controller
plus **CST816D** capacitive touch — on the **nano-f411** board
(STM32F411CEU6 @ 100 MHz). Test patterns follow
`st7365_md350_320x480`, driven by **hardware SPI1 only** (the vendor
example likewise uses the SPI peripheral — there is no software
bit-bang in it), plus touch sensor printout on the serial port.

## Driving the NV3030B (wrapped-command SPI)

The NV3030B uses a **QSPI-compatible single-lane protocol**: every command
is written as a wrapped transaction — CS low, then four bytes
**`02 00 <cmd> 00`** — and parameter/pixel bytes stream into the same CS
frame afterwards. This is the vendor example's exact framing
(`STM32_TK018F3716_hard_spi_captouch`).

**About the "DC/MISO" pin (PA6):** checked in the vendor example —
`spi.h` defines `LCD_DC = GPIO_Pin_6`, but **PA6 is never configured nor
driven anywhere**; the wrapped framing carries command/data in the
transaction itself, so DC is not needed. `DrawPixel` redundantly sets PA6
once, which has no effect. Conclusion: leave PA6 unconnected/unconfigured.
(The module's connector labels this pin DC/MISO depending on the doc; it
is not usable as a read-back MISO — the panel is write-only through this
interface.)

- **Init sequence** (vendor-verbatim): command-set enable (0xFD), gate/
  source timing (0x61..0x64), VSP/VSN (0x65/0x66), gamma and power
  registers (0x67/0x68/0xB1/0xB4/0xB5/0xB6/0xDF/0xE2/0xE5/0xE1/0xE4/
  0xE0/0xE3/0xE6/0xE7/0xE8/0xEC/0xF1/0xF6), command-set disable (0xFD),
  COLMOD 16bpp (0x3A = 0x05), **MADCTL 0x08**, TE polarity (0x35),
  **INVON** (0x21, IPS), sleep out (0x11), display on (0x29).
- **Geometry**: 240x284, windows at COL_Pre = 0, ROW_Pre = 0 (full range
  addressed directly). The vendor maps touch Y as `284 - Y`.
- **No reset pin** on this module: the vendor sequence settles CS
  (high, then low) for 100 ms before the init commands; no RST GPIO.
- **No backlight pin** in this wiring: the module's backlight is
  hardwired on (the vendor drives BL from a different pin that is not
  part of this connector wiring).

## Touch: CST816D over I2C

The F411 has **no hardware I2C on PA2/PA3** (they are USART2 pins), so
the touch bus is **bit-banged** (open-drain SDA, push-pull SCL).

- 7-bit slave address **0x15** (byte on the wire: 0x2A write / 0x2B read).
- Touch data block: 8 bytes from register 0x00. **Byte 3 = 0x80** marks an
  active touch; **byte 4 = X** (8 bit, panel 240 wide); bytes 5:6 =
  **Y** (12 bit, panel 284 tall).

## Wiring

| LCD pin | MCU pin | Feature |
| ------- | ------- | ------- |
| SCL | PA5 | SPI clock (HW SPI1_SCK, AF5) |
| SDA | PA7 | SPI data out (HW SPI1_MOSI, AF5) |
| CS  | **PA4** | Chip select (GPIO software CS) |
| DC  | PA6 | **not used** by the wrapped protocol (vendor leaves it floating too) |
| D2/D3 | - | not connected (QSPI lanes; the F411 has no QSPI) |
| RST | - | **no reset pin on this module** (vendor settles CS instead) |
| BL  | - | **not wired** (module backlight is on whenever powered) |

| Touch pin | MCU pin | Feature |
| --------- | ------- | ------- |
| TOUCH_SCL | PA2 | I2C clock (bit-banged) |
| TOUCH_SDA | PA3 | I2C data (bit-banged, open-drain) |

## What it does

The demo loops forever on the HW SPI1 bus, running the **full pattern set** — TEST_STAND (frame / 16-level gray / bands / solid colors, timed),
info pages (normal + inverted), HSV gradient sweep, LED test — with a
live FPS counter, plus **touch printout on the serial port**
(`[TOUCH] down X=.. Y=..` on touch, `[TOUCH] release` on lift).

Measured solid fills (320x284 = 68,160 px): ~142 ms per fill at the
12.5 MHz isolation rate on HW SPI1 (wire time alone is ~82 ms, so the
per-byte polling overhead is already visible here; DMA would be
the next step).

## Build / flash / console

```bash
cd app/nv3030b_md183_240x284_cst816d
bash build.sh          # == mkdir build && cd build && cmake -G Ninja .. && ninja
ninja flash            # probe-rs download + reset over ST-Link SWD
```

Console is the board's USART1 / ST-Link VCP (COM9, 115200 8-N-1); a banner
prints once at boot and the pattern phases log as they run, looping forever.

> **"Target voltage (VAPP) is 0.02 V" warning** during `ninja flash` can be
> ignored - it is just the ST-Link's target-voltage sensing reporting an
> unreliable reading. Flashing, reset and the VCP console all work fine
> (verified on hardware).

## Files

- `src/main.c` - pattern set on both buses + touch printout
- `src/lcd.c` / `lcd.h` - NV3030B init + 240x284 geometry + drawing API +
  `LCD_Reinit`
- `src/interface.c` / `interface.h` - wrapped-command bus primitives
  (HW SPI1 wrapped-command writes) + bus init
- `src/touch.c` / `touch.h` - CST816D bit-banged I2C touch driver
- `src/lcd/lcd_fonts.c` / `lcd_fonts.h` - ASCII 6x12 font
- `src/lcd/lcd_font_1608.c` / `lcd_font_1608.h` - ASCII 8x16 banner font
- `src/blockwrite/blockwrite.h` - pixel-window helper