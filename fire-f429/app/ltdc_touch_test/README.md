# LTDC 5-inch touch LCD test - fire-f429

The single (SDRAM-based) demo for the on-board 5-inch 800x480 RGB888 touch
LCD, modeled on the disco-F769 `lcd_touch_test_qspi` demo. The LCD always
needs SDRAM for its framebuffer, so there is no bare (internal-SRAM) variant.

## Behavior (mirrors the F769 reference demo)

- **Bouncing shapes**: 10 squares / circles / triangles bounce around the
  animation area.
- **Pure colors**: red/green/blue/yellow/cyan/magenta/white/black, one after
  the other (prints `[LCD] pure color N`).
- **Gradient**: full HSV hue sweep across the animation area.
- **FPS + touch band**: a status band at the bottom shows `FPS:NNN | T:x,y`
  using a 5x7 ASCII font, and the FPS is also printed on the serial console.
- **Touch**: a white ring + green dot follows the finger; coordinates print
  on the serial console (`Touch: X=.. Y=..` / `Touch: released`).
- Startup shows a solid red full-screen frame for 3 s as an isolation check.

## Result (measured on hardware)

| Metric | Value |
| ------ | ----- |
| Framebuffer | `0xD0000F10` (linker-placed `.sdram_fb` in SDRAM) |
| FPS (800x480 RGB888, shapes + band) | ~37 |
| Touch PID | `0x3931` = "91" (GT911-family) |
| Touch | working, coordinates verified |

## Memory model

`SystemInit()` initializes SDRAM through the HAL before the C runtime;
`.data`, `.bss`, heap **and** the LTDC framebuffer all live in the SDRAM
`.sdram_fb` linker section.

## Wiring

- LTDC RGB888 data + HSYNC/VSYNC/DE/CLK, AF14/AF9 GPIO mapping.
- Pixel clock from PLLSAI (N=420, R=6, DIVR=/8).
- 5-inch panel timing: HBP=46 VBP=23 HSW=1 VSW=3 HFP=40 VFP=13.
- Backlight: **LCD_BL = PD7** (GPIO HIGH = on).
- LCD enable: **DISP = PD4** (GPIO HIGH = on).
- Touch (GT911-family, 8-bit addr `0xBA`): **bit-banged I2C2** on PH4=SCL /
  PH5=SDA (the STM32 hardware I2C is unreliable with Goodix controllers),
  RST=PD11, INT=PD13. INT is held low during reset to select the `0xBA`
  address. The 800x480 config is uploaded at startup (`Touch_LoadConfig`).

## Build and flash

```bash
cmake -G Ninja -B build .
ninja -C build
ninja -C build flash
```