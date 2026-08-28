# ov7670_to_lcd - OV7670 (no-FIFO) camera -> LTDC (fire-f429)

Like `app/ov5640_to_lcd_clone` but for the **OV7670** sensor module that is
now plugged into the same camera connector:

- The ov7670 module has **no FIFO chip** -> the sensor drives the STM32
  **DCMI** interface directly (8-bit parallel RGB565), same capture path as
  the OV5640 app (QVGA 320x240 snapshot into a private SRAM buffer, CPU blit
  + ping-pong swap at vblank).
- The ov7670 module has **no crystal / clock source** -> the MCU **PA8 (MCO1)
  outputs the HSE clock as XCLK**. The fire-f429 HSE is 25 MHz; we use
  **HSE/2 = 12.5 MHz** (`RCC_MCODIV_2`) because the ALIENTEK register table
  is tuned for a 12 MHz XCLK. (25 MHz `RCC_MCODIV_1` is also inside the
  OV7670's 10-48 MHz spec.)

## ⭐ Bring-up findings (hardware-verified, 2026-08-28)

This app originally showed `OV7670 not detected (ID 0000)` / `ffff` and then
`frames: 0`. Everything turned out to be **three separate issues** - the
debug apps `app/ov7670_sccb_test` (minimal SCCB) and `app/ov7670_dcmi_probe`
(polarity + color-bar) were used to isolate each:

1. **The OV7670 does NOT answer the STM32 hardware I2C peripheral's
   repeated-start read.** Its SCCB needs the **classic two-phase transfer**
   (STOP between the register-address write and the data read) + the slow
   ~50 µs half-clock. `bsp_i2c.c` is therefore a **bit-banged SCCB on
   PB6/PB7** (ported from the ALIENTEK reference `sccb.c`), not HAL I2C.
   Verified: `PID=0x76 VER=0x73`, `MIDH=0x7F MIDL=0xA2` (genuine OmniVision).

2. **The DCMI data/sync pins MUST NOT be configured before the SCCB probe.**
   Configuring the AF13 + PULLUP pins (or raising PWDN first) before reading
   the ID kills the sensor's SCCB response. The app therefore does the SCCB
   bring-up with **only** RST (PG2) + PWDN (PG3) outputs first
   (`OV7670_SCCB_MinInit`), reads the ID, THEN initializes the LCD/SDRAM,
   and only after re-verifying the ACK configures the DCMI pins.

3. **DCMI polarity: VSYNC=HIGH, HREF=LOW** (not "HREF active high" - that's
   the OV7670's own COM10 default but on this board/module the capture works
   with HSPOL=LOW; the polarity sweep showed HREF=LOW captures 100% data,
   HREF=HIGH captures zeros). PCKPOL=RISING.

4. **Pixel format: `COM15` must be `0xD0`, not `0x10`.** The ALIENTEK table
   leaves `COM15=0x10` = RGB565 with LIMITED output range 0x10..0xF0, which
   makes the image dark/blocky ("color curve"). The color-bar A/B test proved
   `COM15=0xD0` (full range 00..FF) + CLKRC rewrite produces exact
   `0xFFFF`(white) / `0x0000`(black) bar values. `OV7670_Config()` writes
   this override after the table.

5. **Hardware contacts matter**: the first "no D0/some lines flat" symptom
   was a poor connector contact. After reseating, all 8 data lines toggle.

Verified end state: `readback COM7=0x14 COM15=0xd0 PID=0x76 VER=0x73`,
**~12-13 FPS** live on the 800x480 LCD, clean colors.

## Layout

| File | What |
| ---- | ---- |
| `src/main.c` | init + main loop: SCCB bring-up (minimal pins) -> MCO1 XCLK -> LCD -> DCMI snapshot, ping-pong blit + swap, ASCII FPS |
| `src/lcd_camera.c/.h` | two-layer LTDC via the vendored HAL (RGB565 camera layer + ARGB8888 text layer), board SDRAM + board LTDC pin/clock config |
| `src/bsp_ov7670.c/.h` | OV7670 driver: register table (QVGA RGB565) + COM15 full-range override, DCMI snapshot capture into `snap_buf`, frame flag, resume, IRQ handlers |
| `src/bsp_i2c.c/.h` | **bit-banged SCCB** on PB6/PB7, ~50 µs, classic 2-phase (STOP between addr-write and data-read). The HW I2C repeated-start does NOT work on the OV7670 |
| `src/bsp_debug_usart.h` | stdio stub (the OV5640 template's polling-UART header isn't needed here) |

Reference drivers consulted (adapted, not copied verbatim):
- `D:\board_database\main-mini-stm32-v3\...\扩展实验9 摄像头实验\HARDWARE\OV7670` (registry-table source: QVGA RGB565 register values + bit-bang SCCB)
- `D:\esp32-demo\s3-n16r8-ov5640\camera_stream\managed_components\espressif__esp32-camera\sensors\ov7670.c` (OpenMV-style init: COM7 reset, register arrays, window/scaling, COM15=0xD0 + CLKRC rewrite)

## OV7670 specifics vs the OV5640 template

| Item | OV5640 clone | OV7670 here |
| ---- | ------------ | ----------- |
| SCCB address | 16-bit regs, write addr `0x78` | **8-bit regs**, write addr `0x42` (0x21 << 1) |
| Product ID | PIDH `0x56` @ `0x300A` | PID `0x76` @ `0x0A`, VER `0x73` @ `0x0B` |
| XCLK | module crystal (24 MHz input) | **PA8 MCO1 = HSE/2 = 12.5 MHz output** (required for SCCB too!) |
| Reset | PG2 (board-specific) | same PG2 (board connector), PWDN PG3 |
| AF | AD5820 firmware (`ov5640_AF.c`) | none (no autofocus) |
| Register addr width | 16-bit | **8-bit** |
| SCCB / I2C | HAL I2C repeated-start OK | **bit-bang only; HAL I2C fails** |
| COM15 (RGB565 range) | n/a | **must be 0xD0 (full range), not 0x10** |

## Capture architecture

- **VGA 640x480 RGB565** (sensor max resolution; iwatake2222/OpenMV no-FIFO
  VGA recipe: `COM7=0x04`, `COM3=0x00`, `COM14=0x00`, `PCLK_DIV=0xF0`,
  window `frame_control(158,14,10,490)` + `COM15=0xD0` full range):
  the frame (614400 B = **153600 words**) exceeds the 16-bit DMA NDTR, so
  the capture uses a **4-quarter double-buffer (DBM)** DMA path.
- Frame buffer is **fixed in SDRAM at 0xD0300000** (after the LTDC FBs +
  text region; DMA2 CAN reach SDRAM on F429) - 614400 B, no .bss.
- **DCMI**: `VSPOL=HIGH`, `HSPOL=LOW`, `PCKPOL=RISING`
  (VGA pixels flow on both; see bring-up notes).
- The DCMI DMA is **CONTINUOUS + CIRCULAR** with a deterministic
  **quarter-toggle** override of the HAL callbacks:
  M0 writes Q0↔Q2 (base, +307200), M1 writes Q1↔Q3 (+153600, +460800), so
  the ring fills Q0,Q1,Q2,Q3 forever (each = 153600 B). The DCMI FRAME
  interrupt fires once per sensor frame; HAL_DCMI_FrameEventCallback
  re-enables it and sets a flag.
- **DCMI IRQ is self-contained** (`DCMI_IRQHandler`): it clears ERR/OVR
  and handles FRAME only. The HAL's `HAL_DCMI_IRQHandler` is NOT called -
  at VGA rates its error branch aborts the DMA and re-triggers the IRQ in
  an **infinite ISR storm** (verified via debugger). LINE/VSYNC/ERR/OVR
  DCMI ITs are disabled after `HAL_DCMI_Init` (HAL enables them; LINE
  fires ~480x/frame).

### Display mode: 1:1 full-height (no stretching)

The 640x480 frame fills the full 480-line LCD height, horizontally centered
(X = (800-640)/2 = 80); the side margins stay the blue fill. No scaling
artifacts. **~9-10 FPS** (VGA is 4x the pixels of QVGA; the DCMI at 9-10
FPS is the sensor max for this XCLK).

### Boot test pattern (sensor built-in, 5 s)

After `OV7670_Init()`, the app enables the OV7670's **internal 8-color
test pattern** (`0x70=0x00, 0x71=0x81`) for **5 seconds** - real frames
through the identical DCMI path, then live capture. It proves the DCMI
path, the 1:1 blit and the colors are right before showing a real scene.

- The main loop copies the newest frame into a **ping-pong** display
  framebuffer (CAM0 @ 0xD0000000, CAM1 @ +768000) and swaps at the vertical
  blanking (`LcdCamera_SetLayer0FB`).
- Text: a transparent ARGB8888 overlay (`LcdCamera_AsciiString`, 5x7 font at
  3x) shows the mode line + per-second `Frames:xx FPS` (~9-10).

## Build & flash (gcc default)

```bash
cd app/ov7670_to_lcd
cmake -G Ninja -B build .            # gcc (default)
ninja -C build                       # -> ov7670_to_lcd.hex/.bin
ninja -C build flash                 # OpenOCD via the Keil ULINK2 (SWD)
```

Toolchain defaults (overridable with `-D`):
- `ARM_GCC_ROOT`  = `D:/Arm/GNU Toolchain mingw-w64-x86_64-arm-none-eabi`

## Notes / likely bring-up knobs

1. **XCLK** is 25 MHz (HSE). The ALIENTEK module used 12 MHz (TIM2 CH1 PWM)
   and the ESP32 camera board uses 24 MHz; OV7670 datasheet range is
   10-48 MHz so 25 MHz is in range. If the picture is unstable, halve it:
   `HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_HSE, RCC_MCODIV_2)` (12.5 MHz)
   - or switch to `RCC_MCO1SOURCE_PLLCLK`.
2. **Frame window/scaling** comes from the ESP32 OpenMV driver
   (`SCALING_XSC/YSC/DCWCTR/PCLK_DIV = 0x3A/0x35/0x11/0xF1`,
   `frame_control(158,14,10,490)`) which is the no-FIFO-friendly path.
   The ALIENTEK `ov7670_init_reg_tbl` (QVGA RGB565) provides the base AEC/AGC
   /AWB/gamma values and is fully compatible with a plain parallel readout.
3. **Output format / polarity**: `COM7=0x04` (RGB565 QVGA) + `COM15=0xD0`
   (RGB565, range 00-FF). DCMI is configured to match the sensor's VSYNC
   (HIGH) / HREF (LOW) / PCLK (rising) - same DCMI config as the proven
   OV5640 clone; if the image is shifted/mirrored, adjust in
   `OV7670_Init()` (`HSPolarity`, `MVFP` mirror/flip) or
   `OV7670_Config()` COM3/COM10.
4. **PWDN (PG3)**: the ov7670 module on this connector, like the OV5640,
   asserts PWDN high to power it off then low; RST is PG2 (as in the OV5640
   clone). If your module's RST/PWDN are not wired, it still runs (internal
   pull keeps it alive) - the SCCB ID read is the real "is it alive" check.