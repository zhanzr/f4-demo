# LTDC 5-inch touch LCD test - fire-f429 (bare)

Drives the on-board 5-inch 800x480 RGB888 touch LCD through the STM32F429
LTDC controller (HAL) with a GT1151QM touch controller on I2C2.

Behavior (adapted from the vendor LTDC example and the disco-F769
`lcd_touch_test_qspi` demo):

- 8 colored squares bounce around the screen (English UI, simple fills).
- A blue theme identifies this build.
- Touch draws a white square with a blue inner square at the touch point;
  coordinates print on the serial console.
- FPS is measured over 2-second windows and printed on the console.
- A blue status band at the bottom of the screen identifies the build.

Memory model: application `.data/.bss` live in internal SRAM (default linker
script). The 800x480 RGB888 framebuffer (1.125 MiB) does not fit in internal
SRAM, so the linker places it in a `.sdram_fb` section at `0xD0000000`, and
the bare build initializes SDRAM at runtime before the LTDC writes the first
frame.

## Result (measured on hardware)

| Metric | Value |
| ------ | ----- |
| Framebuffer | `0xD0000000` (linker-placed `.sdram_fb`) |
| FPS (800x480 RGB888, 8 shapes + band) | **38** |
| Fill rate | ~1,166,000 pixels/s |

## Result comparison with `app/ltdc_touch_test`

Both builds run at the same **37 FPS** with the same workload. In this demo
the CPU fills the framebuffer byte-by-byte, so the bottleneck is the CPU fill
loop (and to a lesser degree SDRAM write bandwidth), not framebuffer address
placement within SDRAM. Both projects write the framebuffer through the same
SDRAM, which is why the two setups show no significant difference.

## Wiring (from the vendor example)

- LTDC RGB888 data + HSYNC/VSYNC/DE/CLK, AF14/AF9 GPIO mapping.
- Pixel clock from PLLSAI (N=420, R=6, DIVR=/8).
- 5-inch panel timing: HBP=46 VBP=23 HSW=1 VSW=3 HFP=40 VFP=13.
- GT1151QM touch: I2C2 PH4=SCL PH5=SDA, RST=PD11, INT=PD13.
- Backlight enable: **LCD_BL = PD7** (GPIO output, HIGH = on).
- LCD enable: **DISP = PD4** (GPIO output, HIGH = on).

> ⚠ Both PD7 (backlight) and PD4 (DISP) must be driven HIGH or the panel
> stays dark even though the LTDC is running. The demo shows a solid red
> full-screen frame for 5 seconds at startup to confirm the panel is alive
> before starting the animation.

## Build and flash

```bash
cmake -G Ninja -B build .
ninja -C build
ninja -C build flash
```