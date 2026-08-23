# LTDC 5-inch touch LCD test - fire-f429

The single (SDRAM-based) demo for the on-board 5-inch 800x480 RGB888 touch
LCD, modeled on the disco-F769 `lcd_touch_test_qspi` demo. The LCD always
needs SDRAM for its framebuffer, so there is no bare (internal-SRAM) variant.

## Touch-driven state machine

The demo advances through stages with a tap on the screen:

```
moving objects -> pure colors 1..8 -> gradients 1..8 -> moving objects ...
```

- **Moving objects**: 10 squares / circles / triangles bounce around.
- **Pure colors**: red/green/blue/yellow/cyan/magenta/white/black.
- **Gradients**: 8 static HSV hue-sweep patterns.
- The status band shows the current stage (`MOVE - tap`, `COL n/8 - tap`,
  `GRAD n/8 - tap`) plus `FPS:NNN | T:x,y` in a 5x7 ASCII font.
- A white ring + green dot follows the finger; coordinates print on the
  serial console.

## Double buffering

The moving-object stage (and every stage) renders into a **back buffer** and
then flips the LTDC layer frame-buffer address at the next vertical blanking
interval (`HAL_LTDC_SetAddress_NoReload` + `HAL_LTDC_Reload(VBLANK)`). This
eliminates the flicker/tearing of drawing directly into the displayed buffer.
Both 800x480 RGB888 buffers (2 x 1.125 MiB) live in the `.sdram_fb` section.

## Result (measured on hardware)

| Metric | Value |
| ------ | ----- |
| Framebuffers | `0xD0001040` .. `0xD0233840` (2 x linker-placed) |
| Rendering | double buffered, vsync swap |
| FPS | ~60 (16 ms per frame) |
| Touch PID | `0x3931` = "91" (GT911-family) |
| Touch | working, state machine driven by taps |

## Memory model

`SystemInit()` initializes SDRAM through the HAL before the C runtime;
`.data`, `.bss`, heap **and** the LTDC framebuffers all live in the SDRAM
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