# jd9851_md140_240x240_cst816d - JD9851 1.4" 240x240 LCD + CST816D touch

Drives the **MD140** 1.4" **240x240** module — **JD9851** LCD controller
(ST7789-compatible claim) plus **CST816D** capacitive touch — on the
**nano-f411** board (STM32F411CEU6, APB2 overridden to 100 MHz). Test
patterns follow `nv3030b_md183_240x284_cst816d` / `st7365_md350_320x480`,
driven by **hardware SPI1 @ 50 MHz** (HW-only bring-up build), plus
touch sensor printout on the serial port.

Vendor example: `STM32_LCD_TK014F1828_1246_hardSPI_hal_captouch`
(F103, SPI1 mode 3, prescaler /2 = 36 MHz there).

## Driving the JD9851 (DC-based 4-wire SPI)

Unlike the wrapped-command NV3030B, this panel uses a **plain DC-based
protocol**: the command byte goes out with **DC(RS) low**, every data
byte with **DC high**. CS frames a command + its data bytes (the vendor
leaves CS low across whole sequences; per-transaction framing is a
superset the panel accepts).

- **Init sequence** (vendor-verbatim): password unlock (`0xDF =
  98 51 E9`), page select (`0xDE`), power/DCDC/gamma/timing registers
  across PAGE0/PAGE2 (`0xB7/0xC8/0xB9/0xBB/0xBC/0xC0/0xC1/0xC3/0xC4/
  0xD0/0xD7`, page 2: `0xB8/0xC1`), **sleep out with 120 ms**, oscillator
  + MIPI timing (page 2: `0xC5/0xCA`), **INVON** (0x21, IPS), COLMOD
  16bpp (`0x3A = 0x55`), **MADCTL 0x00**, display on (0x29).
- **Geometry**: 240x240, windows at COL_Pre = 0, ROW_Pre = 0 (full
  range addressed directly).
- **No reset pin** on this module: the vendor sequence settles CS
  (high, then low) for 100 ms before the init commands. `LCD_Reinit`
  re-runs display-off + the full sequence (the password preamble
  restarts the register config).
- **No backlight pin** in this wiring: the module's backlight is
  hardwired on.
- **SPE gotcha** (carried over from the nv3030b work): SPI1 SPE is
  enabled *before* CS goes low — with SPE=0 SCK idles low and enabling
  it with CPOL=1 creates a rising SCL edge the panel would latch as a
  spurious first bit.

## Touch: CST816D over I2C

Same driver as the MD183 module (vendor confirms the same 8-byte touch
block at I2C address 0x15): the F411 has **no hardware I2C on PA2/PA3**
(they are USART2 pins), so the touch bus is **bit-banged** (open-drain
SDA, push-pull SCL). Byte 3 = 0x80 marks an active touch; byte 4 = X,
bytes 5:6 = Y.

## Wiring

| LCD pin | MCU pin | Feature |
| ------- | ------- | ------- |
| SCL | PA5 | SPI clock (HW SPI1_SCK, AF5) |
| SDA/MOSI | PA7 | SPI data out (HW SPI1_MOSI, AF5) |
| CS  | **PA4** | Chip select (GPIO software CS) |
| DC  | **PA6** | Command/data select (GPIO; low = command, high = data) |
| RST | - | **no reset pin on this module** (vendor settles CS instead) |
| BL  | - | **not wired** (module backlight is on whenever powered) |

| Touch pin | MCU pin | Feature |
| --------- | ------- | ------- |
| TOUCH_SCL | PA2 | I2C clock (bit-banged) |
| TOUCH_SDA | PA3 | I2C data (bit-banged, open-drain) |

## What it does

The demo loops forever on the HW SPI1 bus @ 50 MHz, running the **full
pattern set** — TEST_STAND (frame / 16-level gray / bands / solid
colors, timed), info pages (normal + inverted, with the solid-fill
durations), HSV gradient sweep, LED test — with a live FPS counter,
plus **touch printout on the serial port** (`[TOUCH] down X=.. Y=..`
on touch, `[TOUCH] release` on lift).

Measured solid fills (240x240 = 57,600 px): ~23 ms per fill at 50 MHz
(wire floor ~18 ms — the SPI is the bottleneck, thanks to the
register-level `LCD_FillBulk` path carried over from the nv3030b
project).

## Build / flash / console

```bash
cd app/jd9851_md140_240x240_cst816d
bash build.sh          # == mkdir build && cd build && cmake -G Ninja .. && ninja
ninja flash            # probe-rs download + reset over ST-Link SWD
```

Console is the board's USART1 / ST-Link VCP (COM9, 115200 8-N-1); a
banner prints once at boot and the pattern phases log as they run,
looping forever.

> **"Target voltage (VAPP) is 0.02 V" warning** during `ninja flash`
> can be ignored - it is just the ST-Link's target-voltage sensing
> reporting an unreliable reading. Flashing, reset and the VCP console
> all work fine (verified on hardware).

## Files

- `src/main.c` - HW pattern loop + touch printout + APB2 clock override
- `src/lcd.c` / `lcd.h` - JD9851 init + 240x240 geometry + drawing API +
  `LCD_Reinit`
- `src/interface.c` / `interface.h` - DC-based 4-wire bus primitives
  (HW SPI1 @ 50 MHz) + `LCD_FillBulk`
- `src/touch.c` / `touch.h` - CST816D bit-banged I2C touch driver
- `src/lcd/lcd_fonts.c` / `lcd_fonts.h` - ASCII 6x12 font
- `src/lcd/lcd_font_1608.c` / `lcd_font_1608.h` - ASCII 8x16 banner font
- `src/blockwrite/blockwrite.h` - pixel-window helper
