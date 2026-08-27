/**
  * @file    ov7670_to_lcd/src/main.c
  * @brief   OV7670 (no FIFO) camera -> 5" 800x480 LCD on the fire-f429 board.
  *
  * The ov7670 user module has no FIFO chip and no crystal, so:
  *   - PA8 (MCO1) outputs the 25 MHz HSE clock as XCLK
  *     (OV7670_XCLK_Init, before the sensor powers up),
  *   - the sensor's parallel 8-bit RGB565 goes straight into the DCMI,
  *   - DCMI SNAPSHOT mode captures one complete, VSYNC-aligned QVGA
  *     320x240 RGB565 frame per transfer into snap_buf (single-buffer DMA,
  *     no tearing),
  *   - the frame is displayed 1:1 (NOT stretched) inside a centered
  *     window; the margins stay the blue fill,
  *   - on boot the sensor's built-in 8-color test pattern is shown for
  *     ~5 s (real frames through the same capture path), then live
  *     capture,
  *   - the main loop copies the newest frame into a ping-pong display
  *     framebuffer (CAM0/CAM1) and swaps it at the vertical blanking,
  *   - a transparent ARGB8888 text overlay shows the mode line and the
  *     per-second "Frames:xx FPS" line (simple 5x7 ASCII renderer).
  *
  * Flow: camera SCCB bring-up (minimal pins) -> ReadID (0x76/0x73) ->
  *   LCD init -> OV7670_Config (QVGA RGB565 table + COM15=0xD0) ->
  *   DCMI pins + OV7670_Init (snapshot) -> 5 s sensor test pattern ->
  *   per-frame blit + swap, per-second FPS line.
  */

#include "board.h"
#include "lcd_camera.h"
#include "bsp_ov7670.h"
#include "bsp_i2c.h"
#include <stdio.h>

/* The camera driver increments this in the VSYNC callback. */
uint8_t fps = 0;

#define COL_WHITE       0xFFFFFFFFU
#define COL_YELLOW      0xFFFFFF00U
#define COL_TRANSPARENT 0x00FFFFFFU   /* transparent ARGB8888 */

/* Two ping-pong display framebuffers (RGB565 800x480 = 768000 B each). */
#define FB0            LCD_FB_CAM0
#define FB1            LCD_FB_CAM1
#define TEXT_LINE_H    24U            /* Font24-ish line pitch */

/* ------------------------------------------------------------------ */
/* 1:1 (NOT stretched) display of the QVGA 320x240 snapshot: copy it into
 * a CENTERED window of the 800x480 framebuffer; the surrounding margins
 * stay the blue fill. Two pixels per 32-bit store. */

#define CAM_WIN_X  ((LCD_WIDTH  - (uint16_t)img_width ) / 2U)   /* 240 */
#define CAM_WIN_Y  ((LCD_HEIGHT - (uint16_t)img_height) / 2U)   /* 120 */

static void blit_snap(uint32_t fb)
{
    uint32_t *dst = (uint32_t *)fb;
    const uint8_t *src = snap_buf;

    /* centered window start in 32-bit words: (CAM_WIN_Y)*400 + CAM_WIN_X/2 */
    uint32_t win_row = (uint32_t)CAM_WIN_Y * (LCD_WIDTH / 2U) + (CAM_WIN_X / 2U);

    for (uint16_t y = 0; y < img_height; y++)
    {
        const uint8_t *srow = src + (uint32_t)y * ((uint16_t)img_width * 2U);
        uint32_t *drow = dst + win_row + (uint32_t)y * (LCD_WIDTH / 2U);
        for (uint16_t x = 0; x < img_width; x += 2)
        {
            uint16_t p0 = (uint16_t)(srow[x * 2U] | ((uint16_t)srow[x * 2U + 1U] << 8));
            uint16_t p1 = (uint16_t)(srow[x * 2U + 2U] | ((uint16_t)srow[x * 2U + 3U] << 8));
            drow[x >> 1] = (uint32_t)p0 | ((uint32_t)p1 << 16);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Consume one completed snapshot: blit into the hidden buffer, swap at
 * the vertical blanking, re-arm the next snapshot. Returns the new hidden
 * buffer (the other FB), for ping-pong. */

static uint32_t present_frame(uint32_t back)
{
    blit_snap(back);
    LcdCamera_SetLayer0FB(back);
    OV7670_DCMI_Resume();
    OV7670_DMA_Config((uint32_t)snap_buf, img_width * img_height / 2);
    return (back == FB0) ? FB1 : FB0;
}

/* ------------------------------------------------------------------ */
/* OV7670 built-in 8-color test pattern (regs 0x70/0x71). on=1 enables it,
 * on=0 returns to live image. */

static void sensor_test_bar(uint8_t on)
{
    OV7670_WriteReg(OV7670_REG_SCALING_XSC, 0x00);
    OV7670_WriteReg(OV7670_REG_SCALING_YSC, on ? 0x81 : 0x01);
}

/* ------------------------------------------------------------------ */
/* "Frames:xxx FPS" on the text overlay, line 2 (vendor position).   */

static void draw_fps_line(uint8_t line, uint32_t frames)
{
    char buf[20];
    uint8_t n = 0;
    const char *h = "Frames:";
    while (*h != '\0') { buf[n++] = *h++; }
    /* 3-wide, space-padded, right-aligned: "  5", " 12", "123". */
    if (frames >= 100U)
    {
        buf[n++] = (char)('0' + (frames / 100U) % 10U);
        buf[n++] = (char)('0' + (frames / 10U) % 10U);
        buf[n++] = (char)('0' + frames % 10U);
    }
    else if (frames >= 10U)
    {
        buf[n++] = ' ';
        buf[n++] = (char)('0' + (frames / 10U) % 10U);
        buf[n++] = (char)('0' + frames % 10U);
    }
    else
    {
        buf[n++] = ' ';
        buf[n++] = ' ';
        buf[n++] = (char)('0' + frames % 10U);
    }
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
    OV7670_IDTypeDef OV7670_Camera_ID;

    HAL_Init();
    Board_Init();                /* 180 MHz, LEDs, USART1 console */

    printf("\r\n=== ov7670_to_lcd (OV7670 no-FIFO -> LTDC, QVGA) ===\r\n");

    /* Step 1: camera SCCB bring-up BEFORE any SDRAM/LTDC init - the proven
     * app/ov7670_sccb_test sequence. The DCMI data pins must NOT be
     * configured before the SCCB probe (they interfere with the sensor's
     * SCCB). */
    OV7670_XCLK_Init();                 /* PA8 MCO1 = HSE/2 = 12.5 MHz */
    printf("OV7670 XCLK on PA8 (MCO1 = HSE/2 = 12.5 MHz)\r\n");

    OV7670_SCCB_MinInit();              /* minimal RST/PWDN + power seq */
    I2CMaster_Init();                   /* bit-bang SCCB on PB6/PB7 */

    /* ID check + retry (startup can be flaky) */
    uint8_t detected = 0;
    for (uint8_t attempt = 0; attempt < 4 && !detected; attempt++)
    {
        if (attempt > 0)
        {
            printf("retry %u: power-cycling...\r\n", (unsigned)attempt);
            OV7670_PowerCycle();
            I2CMaster_Init();
        }
        OV7670_ReadID(&OV7670_Camera_ID);
        printf("  ID %02x%02x\r\n",
               (unsigned)OV7670_Camera_ID.PIDH,
               (unsigned)OV7670_Camera_ID.PIDL);
        if (OV7670_Camera_ID.PIDH == 0x76)
        {
            detected = 1;
        }
    }

    /* Step 2: LCD init (two layers: RGB565 camera + ARGB8888 text overlay). */
    LcdCamera_Init();

    /* The sensor might stop ACKing after SDRAM/LTDC init - re-check. */
    if (!(OV7670_ProbeAddr(0x42) == 0))
    {
        printf("sensor not ACKing after LCD init - power-cycling again\r\n");
        OV7670_PowerCycle();
        I2CMaster_Init();
        OV7670_ReadID(&OV7670_Camera_ID);
        detected = (OV7670_Camera_ID.PIDH == 0x76) ? 1U : 0U;
    }

    /* Blue behind the camera image (both ping-pong buffers). */
    LcdCamera_CameraFill(FB0, 0, 0, LCD_WIDTH, LCD_HEIGHT, 0x001FU);
    LcdCamera_CameraFill(FB1, 0, 0, LCD_WIDTH, LCD_HEIGHT, 0x001FU);

    /* Transparent text overlay with the mode line. */
    LcdCamera_TextClear(0, 0, LCD_WIDTH, LCD_HEIGHT);
    LcdCamera_AsciiString(0, 2, "Mode:QVGA 320x240 1:1", COL_WHITE, COL_TRANSPARENT, 3);
    printf("LCD ready\r\n");

    if (!detected)
    {
        LcdCamera_AsciiString(0, (uint16_t)(8 * TEXT_LINE_H), "OV7670 NOT DETECTED",
                              COL_WHITE, COL_TRANSPARENT, 2);
        while (1)
        {
        }
    }

    /* Register table (QVGA RGB565) - bit-bang SCCB. */
    if (OV7670_Config() != 0)
    {
        printf("OV7670_Config failed (SCCB)\r\n");
        while (1)
        {
        }
    }

    /* Now that the SCCB is up and the sensor is running, configure the DCMI
     * data/sync pins (they must NOT be touched before the SCCB probe - that
     * combination is what broke the sensor earlier). */
    OV7670_DCMI_GpioInit();

    /* DCMI snapshot capture: arms the first snapshot into snap_buf. */
    OV7670_Init();

    /* ------------------------------------------------------------------
     * Boot demo: sensor built-in 8-color test pattern for ~5 s (real
     * frames through the normal capture path), then live capture. */
    printf("test pattern ON (5 s)...\r\n");
    sensor_test_bar(1);
    {
        uint32_t back = FB1;
        uint32_t pat_end = HAL_GetTick() + 5000U;
        while ((int32_t)(pat_end - HAL_GetTick()) > 0)
        {
            if (OV7670_FrameState)
            {
                OV7670_FrameState = 0;
                back = present_frame(back);
            }
        }
    }
    sensor_test_bar(0);
    printf("test pattern OFF, live capture\r\n");

    fps = 0;

    uint32_t fb_back  = FB1;
    uint32_t shown = 0;              /* displayed frames (snapshot mode has no
                                        VSYNC IT, so count the blits instead) */
    uint32_t shown_last = 0;
    uint32_t fps_last_tick = HAL_GetTick();

    while (1)
    {
        /* A complete snapshot frame is ready: present it (tear-free ping-pong
         * swap at the vertical blanking) and re-arm the next capture. */
        if (OV7670_FrameState)
        {
            OV7670_FrameState = 0;
            shown++;
            fb_back = present_frame(fb_back);
        }

        /* Per-second FPS line (vendor line 2) - counts displayed frames. */
        if ((HAL_GetTick() - fps_last_tick) >= 1000U)
        {
            fps_last_tick = HAL_GetTick();
            uint32_t per_sec = shown - shown_last;
            shown_last = shown;

            draw_fps_line(2, per_sec);
            printf("frames: %lu\r\n", (unsigned long)per_sec);
        }
    }
}