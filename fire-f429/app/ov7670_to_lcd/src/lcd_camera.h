/**
  * @file    ov7670_to_lcd/src/lcd_camera.h
  * @brief   Two-layer LTDC driver for the live camera display (fire-f429),
  *          built directly on the vendored STM32F4 HAL.
  *
  *   Layer 1 (LTDC_LAYER_1): RGB565 800x480 camera framebuffer at
  *     LCD_FB_CAM0 (0xD0000000). The app ping-pongs between CAM0/CAM1
  *     (LcdCamera_SetLayer0FB swaps at the vertical blanking).
  *   Layer 2 (LTDC_LAYER_2): ARGB8888 800x480 text overlay at
  *     LCD_FB_TEXT (0xD0177000). Pixels with alpha 0x00 are transparent.
  *
  * SDRAM is initialized with the BOARD-proven config
  * (Board_SDRAM_EarlyInit, 8-col/12-row/CAS2 with read-burst enabled).
  */

#ifndef FIRE_F429_OV7670_LCD_CAMERA_H
#define FIRE_F429_OV7670_LCD_CAMERA_H

#include <stdint.h>

#define LCD_WIDTH  800U
#define LCD_HEIGHT 480U

/* Fixed framebuffer addresses in SDRAM (RGB565 frame = 768000 B).
 * Text overlay is ARGB8888 at +1536000 - no overlap with FB1. */
#define LCD_FB_CAM0  0xD0000000U
#define LCD_FB_CAM1  0xD00BB800U
#define LCD_FB_TEXT  0xD0177000U

/* Bring up the LTDC: panel timing, PLLSAI pixel clock, SDRAM, and the two
 * layers above (camera RGB565 + text ARGB8888). */
void LcdCamera_Init(void);

/* Swap the camera layer to `addr` (CAM0 or CAM1) at the next vertical
 * blanking - tear-free ping-pong. */
void LcdCamera_SetLayer0FB(uint32_t addr);

/* Fill a rectangle of a camera framebuffer (LCD_FB_CAM0 or CAM1) with an
 * RGB565 color. */
void LcdCamera_CameraFill(uint32_t fb, uint16_t x, uint16_t y, uint16_t w,
                          uint16_t h, uint16_t rgb565);

/* Clear a rectangle of the text layer to transparent (alpha 0x00). */
void LcdCamera_TextClear(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

/* Draw an ASCII string (5x7 at `scale`, default 2x) on the text layer.
 * fg/bg are ARGB8888; pass bg = 0x00FFFFFF for transparency. */
void LcdCamera_AsciiString(uint16_t x, uint16_t y, const char *str,
                           uint32_t fg, uint32_t bg, uint8_t scale);

#endif /* FIRE_F429_OV7670_LCD_CAMERA_H */