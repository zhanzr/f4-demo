/**
  * @file    ov5640_to_lcd_clone/src/main.c
  * @brief   Stable OV5640 camera -> 5" 800x480 LCD on the fire-f429 board.
  *
  * Consolidation of the vendor 45-OV5640_sensor example behavior (stable
  * capture path, see README) built on the vendored STM32F4 HAL + shared
  * board layer (no copied vendor-example drivers):
  *
  *   - sensor runs QVGA 320x240 RGB565 (the vendor's RGB565_QVGA table),
  *   - DCMI SNAPSHOT mode captures one complete, VSYNC-aligned frame per
  *     transfer into snap_buf (single-buffer DMA, no HAL double-buffer
  *     path, no tearing),
  *   - the main loop blits the frame (nearest 2.5x H / 2x V) into a
  *     ping-pong display framebuffer (CAM0/CAM1) and swaps it at the
  *     vertical blanking,
  *   - a transparent ARGB8888 text overlay shows the mode line and the
  *     per-second "Frames:xx FPS" line (simple 5x7 ASCII renderer).
  *
  * Flow: LcdCamera_Init -> blue fill -> OV5640_HW_Init -> I2C -> ReadID
  *   (0x56) -> RGB565Config (QVGA) -> OV5640_Init (snapshot) -> AUTO_FOCUS
  *   -> per-frame blit + swap, per-second FPS line.
  */

#include "board.h"
#include "lcd_camera.h"
#include "bsp_ov5640.h"
#include "bsp_i2c.h"
#include "ov5640_AF.h"
#include <stdio.h>

/* The camera driver increments this in the VSYNC callback. */
uint8_t fps = 0;

#define COL_WHITE       0xFFFFFFFFU
#define COL_YELLOW      0xFFFFFF00U
#define COL_TRANSPARENT 0x00FFFFFFU   /* transparent ARGB8888 */

/* Two ping-pong display framebuffers (RGB565 800x480 = 768000 B each).
 * CAM0 = LCD_FB_CAM0 (the layer-0 init FB), CAM1 right after it. The text
 * overlay layer lives at +1536000, so no overlap. */
#define FB0            LCD_FB_CAM0
#define FB1            LCD_FB_CAM1
#define TEXT_LINE_H    24U            /* Font24-ish line pitch */

/* ------------------------------------------------------------------ */
/* Blit one QVGA 320x240 RGB565 snapshot into an 800x480 RGB565 display
 * framebuffer, nearest-neighbour 2.5x H / 2x V (sx = x*2/5, sy = y/2). */

static void blit_snap(uint32_t fb)
{
    volatile uint16_t *dst = (volatile uint16_t *)fb;
    const uint8_t *src = snap_buf;

    for (uint16_t y = 0; y < 480; y++)
    {
        const uint8_t *srow = src + (uint32_t)(y / 2) * 640U;   /* 320*2 */
        volatile uint16_t *drow = dst + (uint32_t)y * 800U;
        for (uint16_t x = 0; x < 800; x++)
        {
            uint32_t sx = ((uint32_t)x * 2U) / 5U;              /* 0..319 */
            drow[x] = (uint16_t)(srow[sx * 2U] | ((uint16_t)srow[sx * 2U + 1U] << 8));
        }
    }
}

/* ------------------------------------------------------------------ */
/* "Frames:xxx FPS" on the text overlay, line 2 (vendor position).   */

static void draw_fps_line(uint8_t line, uint32_t frames)
{
    char buf[20];
    uint8_t n = 0;
    const char *h = "Frames:";
    while (*h != '\0') { buf[n++] = *h++; }
    buf[n++] = (char)('0' + (frames / 100U) % 10U);
    buf[n++] = (char)('0' + (frames / 10U) % 10U);
    buf[n++] = (char)('0' + frames % 10U);
    buf[n++] = ' '; buf[n++] = 'F'; buf[n++] = 'P'; buf[n++] = 'S';
    buf[n] = '\0';

    /* clear the whole line band, then draw (scale 3 = 15x21 px, Font24-ish) */
    LcdCamera_TextClear(0, (uint16_t)(line * TEXT_LINE_H), LCD_WIDTH,
                        (uint16_t)(TEXT_LINE_H + 4U));
    LcdCamera_AsciiString(0, (uint16_t)(line * TEXT_LINE_H + 2U), buf,
                          COL_WHITE, COL_TRANSPARENT, 3);
}

/* ------------------------------------------------------------------ */

int main(void)
{
    OV5640_IDTypeDef OV5640_Camera_ID;

    HAL_Init();
    Board_Init();                /* 180 MHz, LEDs, USART1 console */

    printf("\r\n=== ov5640_to_lcd_clone (OV5640 -> LTDC, stable QVGA) ===\r\n");

    /* LCD init: two layers (RGB565 camera + ARGB8888 text overlay). */
    LcdCamera_Init();

    /* Blue behind the camera image (both ping-pong buffers). */
    LcdCamera_CameraFill(FB0, 0, 0, LCD_WIDTH, LCD_HEIGHT, 0x001FU);
    LcdCamera_CameraFill(FB1, 0, 0, LCD_WIDTH, LCD_HEIGHT, 0x001FU);

    /* Transparent text overlay with the mode line. */
    LcdCamera_TextClear(0, 0, LCD_WIDTH, LCD_HEIGHT);
    LcdCamera_AsciiString(0, 2, "Mode:QVGA 320x240", COL_WHITE, COL_TRANSPARENT, 3);
    printf("LCD ready, initializing camera...\r\n");

    /* Camera: HW init, SCCB (I2C1), read ID. */
    OV5640_HW_Init();
    I2CMaster_Init();

    OV5640_ReadID(&OV5640_Camera_ID);

    if (OV5640_Camera_ID.PIDH == 0x56)
    {
        printf("OV5640 ID OK: %02x%02x\r\n",
               (unsigned)OV5640_Camera_ID.PIDH, (unsigned)OV5640_Camera_ID.PIDL);
    }
    else
    {
        LcdCamera_AsciiString(0, (uint16_t)(8 * TEXT_LINE_H), "OV5640 NOT DETECTED",
                              COL_WHITE, COL_TRANSPARENT, 2);
        printf("OV5640 not detected (ID %02x%02x)\r\n",
               (unsigned)OV5640_Camera_ID.PIDH, (unsigned)OV5640_Camera_ID.PIDL);
        while (1)
        {
        }
    }

    /* RGB565 QVGA output + DCMI snapshot capture + auto focus (vendor order).
     * OV5640_Init() already armed the first snapshot into snap_buf. */
    OV5640_RGB565Config();
    OV5640_Init();
    OV5640_AUTO_FOCUS();

    fps = 0;

    uint32_t fb_front = FB0;
    uint32_t fb_back  = FB1;
    uint32_t shown = 0;              /* displayed frames (snapshot mode has no
                                        VSYNC IT, so count the blits instead) */
    uint32_t shown_last = 0;
    uint32_t fps_last_tick = HAL_GetTick();

    while (1)
    {
        /* A complete snapshot frame is ready: present it (tear-free ping-pong
         * swap at the vertical blanking) and re-arm the next capture. */
        if (OV5640_FrameState)
        {
            OV5640_FrameState = 0;
            shown++;

            blit_snap(fb_back);            /* CPU blit into the hidden buffer */
            LcdCamera_SetLayer0FB(fb_back); /* swap at vblank (no tearing)    */
            { uint32_t t = fb_front; fb_front = fb_back; fb_back = t; }

            OV5640_DCMI_Resume();          /* re-enable capture               */
            OV5640_DMA_Config((uint32_t)snap_buf, img_width * img_height / 2);
        }

        /* Per-second FPS line (vendor line 2) - counts displayed frames. */
        if ((HAL_GetTick() - fps_last_tick) >= 1000U)
        {
            fps_last_tick = HAL_GetTick();
            draw_fps_line(2, shown - shown_last);
            shown_last = shown;
        }
    }
}
