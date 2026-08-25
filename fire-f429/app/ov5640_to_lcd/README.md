# ov5640_to_lcd - OV5640 camera on the 5" 800x480 LCD (fire-f429)

Port of the vendor **45-OV5640_sensor** example (F429IG-V1V2) to the
`fire-f429` board structure. The OV5640 image is captured by DCMI + DMA and
shown live on the LTDC panel, with an FPS / status HUD overlay.

## Behavior (mirrors the vendor example)

- **Live camera on the LCD** - the OV5640 RGB565 frames fill the 800x480
  screen and update continuously.
- **FPS overlay** - a title line and an `FPS` / frame-count line are drawn
  over the image (black background, refreshed once per second), like the
  vendor's "帧数:x FPS" line.
- **Missing-camera handling** - if the sensor is not detected over SCCB the
  LCD shows the failure and the program stops (vendor `while(1)` behavior).
- **LEDs** - `LED_G` blinks per displayed frame, `LED_R` lights on a fatal
  camera error.

## Differences from the vendor example (fire-f429 specific)

| Item | Vendor example | This app |
| ---- | -------------- | -------- |
| Camera mode | RGB565 **WVGA 800x480** | RGB565 **QQVGA 160x120** (the proven stable mode on this 24 MHz-crystal module; see below) |
| LCD layers | Layer0 RGB565 camera FB + layer1 ARGB8888 text | Single RGB888 layer; frames are software-stretched 5x H / 4x V to fill the screen |
| SCCB | board-specific (PD12/PD13 or PB6/PB7) | I2C1 PB6/PB7 (fire-f429 wiring) |
| Camera RST | PB5 (F429IG-V1V2 board) | PG2 (**挑战者** fire-f429 board) |

The camera driver (`src/ov5640.c`) is the one proven on this exact board by
`app/eth_http_server`: RGB565 QQVGA 160x120. The module's built-in JPEG
encoder does **not** produce valid output through the F4 DCMI, and the
vendor's larger RGB565 timings were not verified on this board, so the
QQVGA mode + software stretch is used to guarantee a live picture.

## Pins (fire-f429)

```
OV5640:
  DCMI    VSYNC PI5, HSYNC PA4, PIXCLK PA6
          D0..D3 PH9..12, D4 PH14, D5 PD3, D6 PI6, D7 PI7
          PWDN PG3 (low = on), RST PG2
  SCCB    I2C1: SCL PB6, SDA PB7  (address 0x78 / ID 0x56)

LCD (LTDC, 5" 800x480):
  CLK PG7, HSYNC PI10, VSYNC PI9, DE PF10, DISP PD4, BL PD7
  (board_ltdc.c)
```

## Build & flash

```bash
cd app/ov5640_to_lcd
cmake -G Ninja -B build .
ninja -C build          # -> ov5640_to_lcd.hex/.bin, prints size
ninja -C build flash    # OpenOCD via the Keil ULINK2 (CMSIS-DAP, SWD)
```

The board uses a **static** LCD framebuffer in SDRAM (`.sdram_fb`,
`DATA_IN_ExtSDRAM`), like `ltdc_touch_test`.

## Console output (USART1, 115200 8-N-1)

```
=== ov5640_to_lcd on fire-f429 (OV5640 -> LTDC) ===
LCD: 800x480 RGB888, camera: RGB565 160x120
OV5640: init ok (attempt 1, RGB565 160x120)
OV5640: selftest OK - 92 RGB565 frames (160x120), 61 fps
Live camera -> LCD. LED_G blinks per frame, LED_1 per second.
display: 27 fps, 223 frames, camera ready=1
```

Measured on hardware: the camera streams ~60 fps out of DCMI; the display
updates at **~27 fps** (the software RGB888 stretch-blit of 1.15 MiB/frame
into SDRAM is the bottleneck - the vendor's 800x480 RGB565 example runs at
14.2 fps with the same kind of pipeline). The display uses
`OV5640_GetLatestFrame()` (newest-complete-frame) so slow blits simply skip
intermediate frames instead of stalling on the DMA ring wrap.

## Files

- `src/main.c` - init + main loop: camera frame -> stretch-blit -> HUD -> swap
- `src/ov5640.c` / `src/ov5640.h` - camera driver (copied from
  `app/eth_http_server`, proven on this board)
- `CMakeLists.txt` - build + flash (shared board/CMake glue)
