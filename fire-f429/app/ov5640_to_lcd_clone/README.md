# ov5640_to_lcd_clone - OV5640 camera -> LTDC (fire-f429)

The **single consolidated** OV5640-to-LCD camera app for the fire-f429 board
(the earlier `ov5640_to_lcd` and `ov5640_to_lcd_armclang` variants were
merged into this one). It is built on the **vendored STM32F4 HAL** and the
**shared board layer** (`board/board_ltdc.c`, `board/board_sdram.c`, ...) -
no copied vendor-example drivers. Toolchain: **GNU gcc** by default (Keil
AC6 armclang optional).

Stable-capture architecture (validated against the reference
`stm32h750_prj/h750-mini/app/ov5640_to_st7789` project):

- **QVGA 320x240** capture (the vendor's own `RGB565_QVGA` register table).
  The frame (153600 B = 38400 words) fits a single DMA buffer (≤0xFFFF
  words), so **DCMI SNAPSHOT mode** (`DCMI_MODE_SNAPSHOT` + `DMA_NORMAL`)
  captures ONE complete, VSYNC-aligned frame per transfer and the DMA stops
  - no HAL double-buffer path, no tearing.
- The frame lands in a private **SRAM buffer** (`snap_buf`); the LTDC never
  reads a DMA-written buffer.
- The main loop CPU-blits the frame (nearest-neighbour 2.5x H / 2x V) into a
  **ping-pong** display framebuffer (CAM0 @ 0xD0000000, CAM1 @ +768000) and
  swaps it at the vertical blanking (`LcdCamera_SetLayer0FB`).
- Text: a transparent ARGB8888 overlay (`LcdCamera_AsciiString`, 5x7 font at
  3x) shows the mode line + per-second `Frames:xx FPS`.

Lower resolution / lower FPS by design (~7-10 fps displayed, stable).

## Layout

| File | What |
| ---- | ---- |
| `src/main.c` | init + main loop: snapshot consume, ping-pong blit + swap, ASCII FPS |
| `src/lcd_camera.c/.h` | two-layer LTDC via the vendored HAL (RGB565 camera layer + ARGB8888 text layer), board SDRAM + board LTDC pin/clock config |
| `src/bsp_ov5640.c/.h` | OV5640 driver: SCCB-less register tables (base + QVGA), DCMI snapshot capture into `snap_buf`, frame flag, resume, IRQ handlers |
| `src/bsp_i2c.c/.h` | SCCB over I2C1 (PB6/PB7 @ 400 kHz) via the vendored HAL |
| `src/ov5640_AF.c/.h` | vendor AD5820 auto-focus firmware + register commands |
| `src/bsp_debug_usart.h` | tiny stub (debug-UART header for the SCCB wrapper) |

The camera driver (`bsp_ov5640.c`) keeps the vendor register tables and the
auto-focus, but everything LCD/SDRAM uses the shared board + vendored HAL.

## Key fixes (all verified on hardware)

1. **RST = PG2** - the vendor example's reset pin is wrong for the 挑战者
   F429 board; without this SCCB fails and the ID check fails.
2. **Missing IRQ handlers** - the repo startup maps `DCMI_IRQHandler` /
   `DMA2_Stream1_IRQHandler` to `Default_Handler` (infinite loop) unless the
   app defines them; without them the first VSYNC hangs and the capture
   stalls. Both are defined (+ clear `ERRRI`/`OVRRI` before the newer HAL
   aborts the DMA on those errors).
3. **QVGA snapshot instead of WVGA direct-DMA** - the 800x480 path needs the
   newer HAL's flaky >0xFFFF double-buffer; QVGA fits one buffer and
   snapshot mode captures complete frames deterministically.
4. **Board SDRAM config** (`Board_SDRAM_EarlyInit`, 8/12/CAS2, read-burst
   ON) instead of the vendor's (9/13/CAS3, read-burst OFF) - the vendor's
   config starves the LTDC FIFO and **smears/triplicates the image
   horizontally** (the "Hori_A/B/C" bug, also present in the vendor example).
5. **ASCII text** instead of the vendor Font24/GBK path - the vendor's
   renderer garbled the strings (missing glyphs, repeated copies); the
   external-flash Chinese font module is not ported.

## Build & flash (gcc default)

```bash
cd app/ov5640_to_lcd_clone
cmake -G Ninja -B build .            # gcc (default)
ninja -C build                       # -> ov5640_to_lcd_clone.hex/.bin
ninja -C build flash                 # OpenOCD via the Keil ULINK2 (SWD)

# optional armclang (Keil AC6):
cmake -G Ninja -B build-ac6 -DSTM32_TOOLCHAIN=armclang .
ninja -C build-ac6 flash
```

Toolchain defaults (overridable with `-D`):
- `ARM_GCC_ROOT`  = `D:/Arm/GNU Toolchain mingw-w64-x86_64-arm-none-eabi`
- `ARMCLANG_ROOT` = `D:/Keil_v5/ARM/ARMCLANG` (armclang only)

## Verified on hardware (2026-08-26)

```
=== ov5640_to_lcd_clone (OV5640 -> LTDC, stable QVGA) ===
LCD ready, initializing camera...
OV5640 ID OK: 5640
<<-CAMERA-DEBUG->> [278]OV5640_FOCUS_AD5820_Init   (auto-focus runs)
```
- Live: `snap_buf` + both ping-pong FBs update continuously; display FPS
  line counts ~7-10 frames/s; text renders clean at the left; no smearing.
- The FPS line is space-padded (`Frames:  5 FPS`, `Frames: 12 FPS`) and the
  build uses `-O3` with a non-volatile 2-pixels-per-32-bit-store blit (the
  LTDC reads SDRAM directly; F4 has no cache) - faster than the previous
  `-O2` + volatile 16-bit-store blit.

The reference `ov5640_to_st7789` project on the STM32H750 uses the same
pattern (small frame + private buffer + frame-flag + display copy) - that is
the architecture reproduced here.

## Pixel formats

| Item | Format | Bytes/pixel |
| ---- | ------ | ----------- |
| LTDC camera layer | RGB565 | 2 |
| Text overlay layer | ARGB8888 | 4 |
| OV5640 sensor output (QVGA mode) | RGB565 | 2 |
| The H750 ST7789 reference | RGB565 | 2 |

The whole image path is 16-bit RGB565 - the pixel format was NOT the cause
of the triplication.

## The triplication/smearing root cause (found via a static R|G|B bar test)

The "Hori_A/B/C triplication" (right third = copy of the middle, colors
mixing at the boundaries, text stretched/repeated) was NOT in the data, the
layer config, the timing, or the pixel clock - a static color-bar test
proved the framebuffer was clean and the LTDC registers matched the
known-good apps byte-for-byte.

**Root cause: the vendor's SDRAM initialization** (9-col / 13-row, CAS3,
read-burst DISABLED, pipe delay 1) cannot feed the LTDC fast enough → the
LTDC's layer FIFO underruns → pixels get stretched/repeated horizontally
(the "smear" = pixel-boundary mixing, the "wrap" = the stretched line
overflowing the screen). The vendor example's "overlap" bug on this board is
the same thing.

**Fix:** this app uses the board-proven SDRAM init
(`Board_SDRAM_EarlyInit()` from `board/board_sdram.c`: 8-col / 12-row, CAS2,
read-burst ENABLED). Verified with the color-bar test: clean RED | GREEN |
BLUE, and the live camera image + text are clean.

## Armclang (optional) notes

The default toolchain is **GCC**. The repo's shared armclang glue
(`cmake/armclang-keil-toolchain.cmake` + `armclang-postproject.cmake`)
supports building with Keil AC6 (armclang C + GNU as/ld + newlib); it
handles three CMake 3.20 quirks: an empty `-march=`, `armlink` clobbering
`CMAKE_LINKER`, and a stray `--cpu=` on the link line. Note `sprintf` is
specialized by armclang into the ARMCLIB ABI (`__2sprintf`) - this app uses
manual digit formatting, so it builds with both toolchains.
