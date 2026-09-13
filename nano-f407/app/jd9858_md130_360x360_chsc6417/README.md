# jd9858_md130_360x360_chsc6417 - JD9858 round LCD + CHSC6417 touch (FSMC)

Drives the **MD130** 1.3" **360x360 round** module — **JD9858** LCD
controller (datasheet filename says JD5858; ST7789-family register
style with page selection) plus **CHSC6417** capacitive touch — on the
**nano-f407** board (STM32F407VET6 @ 168 MHz). Test patterns follow
`jd9851_md140_240x240_cst816d` / `nv3030b_md183_240x284_cst816d`,
driven over the **FSMC** (a bus the F411 lacks), plus touch data
printed on the serial port.

Vendor example: `STM32_TK0013F1327_LCD_hal_captouch` (F103VET6). The
hardware wiring is identical on the F407VET6 — the FSMC signals land
on the same port pins, so the vendor example ports 1:1.

## Driving the JD9858 (FSMC, 8-bit)

The panel hangs off **FSMC bank 1 / NE1** like a NOR memory: a byte
store to `0x6000_0000` writes a **command** (DC low via A16), a store
to `0x6001_0000` writes **data** (DC high). CS/DC/RD/WR are
hardware-decoded per access — no GPIO bit-twiddling on this bus.

- **Controller**: HAL `HAL_SRAM_Init` (from the vendored
  `drivers/STM32F4xx_HAL_Driver/`, `HAL_SRAM_MODULE_ENABLED` added to
  the board conf): NE1, NOR type, 8-bit width, access mode B,
  ADDSET = 2, DATAST = 5 (vendor HAL settings).
- **Init sequence** (vendor-verbatim): password unlock (`0xDF =
  58 58 B0`), page select (`0xDE`), VCOM/gamma/power/timing tables
  across PAGE0/PAGE4, **sleep out**, oscillator + MIPI timing (page 2),
  COLMOD 16bpp (`0x3A = 0x55`), **MADCTL 0xC0**, display on (0x29).
- **Geometry**: 360x360 round surface, windows at COL_Pre = 0,
  ROW_Pre = 0.
- **Reset**: this module HAS a reset pin — **PD13** (low 100 ms, high
  120 ms); `LCD_Reinit` pulses it and re-runs the sequence.
- **Backlight**: **PA1** (BL_CTR) via **TIM2_CH2 PWM, 1 kHz**
  (\acklight.c\, st7365 API style), default **15%** - the round panel
  is bright. The vendor example never drives it at all.

## Touch: CHSC6417 over bit-banged I2C

- **PB13 = SCL** (push-pull), **PB15 = SDA** (open-drain).
- 7-bit slave address **0x2E** (0x5C write byte).
- Vendor protocol: write register pointer 0x00, STOP, restart, read.
  Coordinate math: `X = ((buf[0] & 0x40) >> 6) << 8 | buf[1]`,
  `Y = ((buf[0] & 0x80) >> 7) << 8 | buf[2]` (9-bit coords).
- The vendor uses **no touch-validity flag**; this port reads 4 bytes
  and prints raw register data + computed coords **on change**
  (`[TOUCH] raw=.. X=.. Y=..`) so the real idle/touch states can be
  identified during bring-up.

## Wiring

| LCD signal | MCU pin | Feature |
| ---------- | ------- | ------- |
| LCD-CS  | PD7  | FSMC NE1 |
| LCD-DC  | PD11 | FSMC A16 (0 = command, 1 = data) |
| LCD-RD  | PD4  | FSMC NOE |
| LCD-WR  | PD5  | FSMC NWE |
| LCD-D0..D7 | PD14, PD15, PD0, PD1, PE7, PE8, PE9, PE10 | FSMC data |
| LCD-RST | PD13 | GPIO output |
| BL_CTR  | PA1  | GPIO output (backlight on/off) |

| Touch pin | MCU pin | Feature |
| --------- | ------- | ------- |
| TOUCH_SCL | PB13 | I2C clock (bit-banged) |
| TOUCH_SDA | PB15 | I2C data (bit-banged, open-drain) |

## What it does

The demo loops forever on the FSMC bus, running the **full pattern
set** — TEST_STAND (frame / 16-level gray / bands / solid colors,
timed), info pages (normal + inverted, with the solid-fill durations),
HSV gradient sweep, LED test — with a live FPS counter, plus the raw
**touch printout on the serial port**.

Measured solid fills (360x360 = 129,600 px): **~14 ms** per fill at
168 MHz (≈18 MB/s on the 8-bit FSMC bus with the vendor timing — the
register-level `LCD_FillBulk` store loop keeps the bus busy).

## Build / flash / console

```bash
cd app/jd9858_md130_360x360_chsc6417
bash build.sh          # == mkdir build && cd build && cmake -G Ninja .. && ninja
ninja flash            # probe-rs download + reset over ST-Link SWD
```

Console is the board's USART1 / ST-Link VCP (115200 8-N-1); a banner
prints once at boot and the pattern phases log as they run, looping
forever.

> **"Target voltage (VAPP) is 0.02 V" warning** during `ninja flash`
> can be ignored - it is just the ST-Link's target-voltage sensing
> reporting an unreliable reading. Flashing, reset and the VCP console
> all work fine (verified on hardware).

## Files

- `src/main.c` - FSMC pattern loop + touch printout
- `src/lcd.c` / `lcd.h` - JD9858 init + 360x360 geometry + drawing API +
  `LCD_Reinit`
- `src/interface.c` / `interface.h` - FSMC bus primitives
  (`HAL_SRAM_Init` + direct bank-window writes) + `LCD_FillBulk`
- `src/touch.c` / `touch.h` - CHSC6417 bit-banged I2C touch driver
- `src/lcd/lcd_fonts.c` / `lcd_fonts.h` - ASCII 6x12 font
- `src/lcd/lcd_font_1608.c` / `lcd_font_1608.h` - ASCII 8x16 banner font
- `src/blockwrite/blockwrite.h` - pixel-window helper
