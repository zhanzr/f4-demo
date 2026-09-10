# bare/lcd_touch_test - 2.8" TFT LCD (ILI9341, 16-bit FSMC) + touch

Ported from the vendored ALIENTEK Apollo examples **tft_lcd_test** (parallel
LCD bring-up) and **实验30 触摸屏实验** (touch + calibration), with the
test-pattern set copied from the **st7789_md169_240x280** reference project.

## Hardware

- Panel: **2.8" TFT, ILI9341 (ID `0x9341`)**, 240x320, on the **16-bit FMC
  NOR/SRAM parallel bus** (bank 1 NE1, `0x60000000`, A18 = RS via `LCD_BASE
  0x60000000|0x7FFFE`). Data/side pins per the vendor `HAL_SRAM_MspInit`
  (PD0/1/4/5/7/8/9/10/13/14/15 + PE7..PE15, AF12 FMC).
- **Backlight: PB5** (push-pull, high = on).
- Touch: vendored auto-detect stack - **GT9147 / FT5206 / OTT2001A**
  (capacitive, bit-banged I2C on PH6/PI3) and **XPT2046-style resistive**
  (bit-banged SPI on PH6/PI3/PI7/PG3/PI8). The 2.8" module uses the resistive
  path, with the 4-point calibration stored in the on-board **AT24C02**
  (bit-banged I2C on PH4/PH5, `EE_TYPE=AT24C02`).

## What it does (loops)

1. `LCD_Init()` reads the ID and runs the vendor ILI9341 init sequence, then the
   vendor **TFTLCD TEST** color loop (12 background colors + text banner).
2. The st7789_md169 patterns: **info page**, **TEST_STAND**
   (`DispFrame`/`DispGrayHor16`/`DispBand`/`DispColor`), **HSV gradient** sweep,
   **LED test**, live **FPS**.
3. **Touch is polled in every phase**: pressing draws a big red point; the
   **RST** corner (top-right) clears the screen. `tp_dev.init()` auto-calibrates
   on first boot (touch the 4 corner crosses) and persists to the EEPROM.

## Build / flash

```bash
bash build.sh            # == cmake -G Ninja .. && ninja
ninja flash              # OpenOCD (ULINK2, SWD)
ninja flash-probe        # probe-rs
```

Console: USART1 PA9/PA10, 115200 8-N-1. Backlight = PB5.

## Layout

- `src/lcd.c/.h` - ILI9341 16-bit FSMC driver (vendor-style + st7789-style API).
- `src/sys_compat.h`, `sys.h`, `delay.c/.h`, `usart.h` - ALIENTEK `sys.h`/`delay.h`
  surface shimmed onto this repo's HAL/board.
- `src/fonts/` - 6x12 + 8x16 ASCII fonts (from the h723-mini / c5 st7789 ports).
- `src/vendor/touch/`, `src/vendor/eeprom/` - the vendored touch + IIC/EEPROM
  drivers (GBK-comment ALIENTEK drop-ins, compile as-is).
- `src/math_shim.c` - a small `sqrt()` so the vendored libm (ARM-state, breaks
  the Thumb veneer under `--gc-sections`) is never linked.

## Notes

- The `stm32f4xx_hal_sram.c` in the vendored HAL tree is inconsistent, so the
  LCD bus is configured **register-level** via the LL FMC driver
  (`FMC_NORSRAM_Init` + timing) instead of `HAL_SRAM_Init`.
- `LCD_DisplayChar`'s non-transparent path was fixed to decode fonts
  row-major/LSB-first (the vendored bit-stream version garbled the 6x12 font).
- `app/lcd_touch_test` reuses these `src/` files as the SDRAM stage-2 app.