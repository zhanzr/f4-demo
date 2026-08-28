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
/* 1:1 (NOT stretched) display of one VGA 640x480 RGB565 frame: copy into
 * the framebuffer. VGA fills the full 480 height; the window is at the
 * LEFTMOST edge (X=0) for bring-up debugging (no centering). */

#define CAM_WIN_X  0U                                            /* debug: left edge */
#define CAM_WIN_Y  0U                                            /* 480 high */

/* ------------------------------------------------------------------ */
/* Normalized pattern renderer (debug).
 *
 * The OV7670's DSP color bar has an ~77.75 px/bar period (not 80) with
 * ~5-10 px interpolation transitions between bars, and the raw capture's
 * phase is arbitrary. Instead of sampling at fixed offsets (which can hit
 * the same bar twice or a transition), we CLASSIFY each pixel into one of
 * the 8 canonical color classes (W/Y/C/G/M/R/B/K) and take the 8 distinct
 * classes in order of first appearance. This is immune to phase, period
 * error, and the red-bar flicker. Then draw 8 solid bars of exactly 80 px
 * each across all 480 rows.
 *
 * Returns 1 if the pattern was found and drawn, 0 if it fell back to the
 * raw blit. */

/* classify an RGB565 word into a bar class index 0..7 (W,Y,C,G,M,R,B,K)
 * or -1 if it's a transition/noise pixel.
 *
 * The thresholds are LOOSE on purpose: the OV7670's actual bar colors are
 * dimmer than the canonical RGB565 values (e.g. red ~0x88a1 = r17, not
 * 0xF800 = r31), so strict thresholds would classify whole bars as
 * "transition" and the renderer would report a missing bar. We classify by
 * which channel DOMINATES (with white = all high, black = all low). */
static int pattern_class(uint16_t w)
{
    uint32_t r = (w >> 11) & 31u, g = (w >> 5) & 63u, b = w & 31u;
    uint32_t mx = r; if (g > mx) mx = g; if (b > mx) mx = b;
    uint32_t mn = r; if (g < mn) mn = g; if (b < mn) mn = b;

    if (mx < 8)  return 7;                    /* K: all low */
    if (mn > 20) return 0;                    /* W: all high */
    if (r > 20 && g > 20 && b < 10) return 1; /* Y */
    if (r < 10 && g > 20 && b > 20) return 2; /* C */
    if (r < 12 && g > 24 && b < 12) return 3; /* G */
    if (r > 20 && g < 14 && b > 20) return 4; /* M */
    if (r > 20 && g < 14 && b < 12) return 5; /* R */
    if (r < 12 && g < 14 && b > 20) return 6; /* B */
    return -1;
}

static uint32_t render_pattern(uint32_t fb)
{
    /* Scan ALL 480 rows and union the classes found. The ring buffer is
     * exactly one frame, so it always contains a complete 8-bar pattern -
     * but the frame boundary sits at an arbitrary phase, so a bar can be
     * split across the buffer wrap. Scanning every row guarantees each bar
     * appears in full somewhere. */
    uint16_t rep[8];
    uint8_t  have[8] = {0,0,0,0,0,0,0,0};
    uint32_t order[8];
    uint32_t ncls = 0;

    for (uint32_t r = 0; r < img_height; r++)
    {
        const uint8_t *row = snap_buf + (size_t)r * 1280u;
        for (uint32_t x = 0; x < 640; x++)
        {
            uint16_t w = (uint16_t)(row[x*2u] | ((uint16_t)row[x*2u+1u] << 8));
            int c = pattern_class(w);
            if (c >= 0)
            {
                if (!have[c])
                {
                    have[c] = 1;
                    rep[c] = w;
                    order[ncls++] = (uint32_t)c;
                }
                else
                {
                    uint32_t s = (w >> 11) + ((w >> 5) & 63u) + (w & 31u);
                    uint32_t sr = (rep[c] >> 11) + ((rep[c] >> 5) & 63u) + (rep[c] & 31u);
                    if (s > sr) rep[c] = w;
                }
            }
        }
    }

    if (ncls != 8)
    {
        printf("pattern: only %lu classes found - raw blit\r\n",
               (unsigned long)ncls);
        /* histogram: how many pixels per class (debug) */
        uint32_t hist[8] = {0,0,0,0,0,0,0,0};
        uint32_t unk = 0;
        for (uint32_t r = 0; r < img_height; r++)
        {
            const uint8_t *row = snap_buf + (size_t)r * 1280u;
            for (uint32_t x = 0; x < 640; x++)
            {
                uint16_t w = (uint16_t)(row[x*2u] | ((uint16_t)row[x*2u+1u] << 8));
                int c = pattern_class(w);
                if (c >= 0) hist[c]++; else unk++;
            }
        }
        printf("pattern hist: W=%lu Y=%lu C=%lu G=%lu M=%lu R=%lu B=%lu K=%lu unk=%lu\r\n",
               (unsigned long)hist[0], (unsigned long)hist[1],
               (unsigned long)hist[2], (unsigned long)hist[3],
               (unsigned long)hist[4], (unsigned long)hist[5],
               (unsigned long)hist[6], (unsigned long)hist[7],
               (unsigned long)unk);
        return 0;
    }

    /* Rotate the class order so WHITE (class 0) is always first. The
     * sensor outputs the bars in the canonical W Y C G M R B K sequence,
     * just rotated by the arbitrary freeze phase; rotating the found order
     * to start at white makes the pattern deterministic every boot. */
    {
        uint32_t wi = 8;
        for (uint32_t k = 0; k < 8; k++)
        {
            if (order[k] == 0u) { wi = k; break; }
        }
        if (wi < 8)
        {
            uint32_t rot[8];
            for (uint32_t k = 0; k < 8; k++)
                rot[k] = order[(wi + k) % 8u];
            for (uint32_t k = 0; k < 8; k++)
                order[k] = rot[k];
        }
    }

    printf("pattern: classes ");
    for (uint32_t k = 0; k < 8; k++)
        printf("%04x ", (unsigned)rep[order[k]]);
    printf("\r\n");

    uint16_t *dst = (uint16_t *)(uintptr_t)fb;
    for (uint32_t y = 0; y < 480; y++)
    {
        uint16_t *rowp = &dst[y * LCD_WIDTH];        /* CAM_WIN_X = 0 */
        for (uint32_t x = 0; x < 640; x++)
            rowp[x] = rep[order[x / 80u]];
    }
    return 1;
}

static void blit_snap(uint32_t fb)
{
    uint32_t *dst = (uint32_t *)fb;
    const uint8_t *src = snap_buf;

    uint32_t win_row = (uint32_t)CAM_WIN_Y * (LCD_WIDTH / 2U) + (CAM_WIN_X / 2U);

    for (uint16_t y = 0; y < img_height; y++)   /* 480 rows */
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
/* Consume one completed frame: blit into the hidden buffer, swap at the
 * vertical blanking, re-arm the next snapshot. Returns the new hidden
 * buffer (the other FB), for ping-pong. */

static uint32_t present_frame(uint32_t back)
{
    blit_snap(back);
    LcdCamera_SetLayer0FB(back);
    OV7670_DMA_Config((uint32_t)snap_buf, img_width * img_height / 2);
    return (back == FB0) ? FB1 : FB0;
}

/* ------------------------------------------------------------------ */
/* OV7670 built-in 8-color test pattern (regs 0x70/0x71). on=1 enables
 * it, on=0 returns to live image.
 *
 * IMPORTANT: only bit7 of SCALING_YSC is the color-bar enable - the lower
 * 7 bits are the VGA/QVGA scaling factors (VGA: XSC=0x3A, YSC=0x35) and
 * must be PRESERVED. Blindly writing the QVGA values (0x00/0x01) here
 * corrupts the VGA geometry -> diagonal lines (the bug this replaces). */
static void sensor_test_bar(uint8_t on)
{
    uint8_t xsc = OV7670_ReadReg(OV7670_REG_SCALING_XSC);
    uint8_t ysc = OV7670_ReadReg(OV7670_REG_SCALING_YSC);

    OV7670_WriteReg(OV7670_REG_SCALING_XSC, (uint8_t)(xsc & 0x7FU));
    OV7670_WriteReg(OV7670_REG_SCALING_YSC,
                    (uint8_t)((ysc & 0x7FU) | (on ? 0x80U : 0x00U)));
}

/* ------------------------------------------------------------------ */
/* Bring-up diagnostics (called from the pattern phase): stop the DMA,
 * measure the REAL row pitch of the captured VGA stream and dump the row
 * structure, then re-arm. Prints are on the console - read them after
 * flashing. */
void ov7670_dump_capture(void)
{
    OV7670_CaptureStop();

    /* 1) autocorrelation for the row pitch (bytes) */
    uint32_t best_p = 0, best_n = 0;
    for (uint32_t p = 1240; p <= 1330; p++)
    {
        uint32_t m = 0, t = 0;
        for (uint32_t i = 0; i + p < img_width * 4U; i++)
        {
            uint8_t a = snap_buf[i];
            if (a == 0 || a == 0xFF) { if (a == 0xFF) { } }
            t++;
            if (snap_buf[i] == snap_buf[i + p]) m++;
        }
        if (t && m > best_n) { best_n = m; best_p = p; }
    }
    {
        /* also probe the 1568 (784 px) hypothesis */
        uint32_t m = 0, t = 0;
        for (uint32_t i = 0; i + 1568 < img_width * 4U; i++)
        {
            t++;
            if (snap_buf[i] == snap_buf[i + 1568]) m++;
        }
        if (m > best_n) { best_n = m; best_p = 1568; }
    }
    printf("CAPTURE pitch: best=%lu B/row (%lu px) match=%lu\r\n",
           (unsigned long)best_p, (unsigned long)(best_p / 2),
           (unsigned long)best_n);

    /* 2) row-structure dump at 1280 B/row: expect the same bar color per
     * column across all rows (vertical bars) if 1280 is right. */
    printf("row dump @1280 B/row (cols c0,c160,c320,c480,c640):\r\n");
    for (uint32_t r = 0; r < 12; r++)
    {
        const uint8_t *row = snap_buf + (size_t)r * 1280u;
        printf("  r%lu:", (unsigned long)r);
        static const uint32_t cols[5] = { 0, 160, 320, 480, 640 };
        for (uint32_t k = 0; k < 5; k++)
        {
            const uint8_t *px = row + cols[k] * 2u;
            uint16_t w = (uint16_t)(px[0] | (px[1] << 8));
            printf(" %04x", (unsigned)w);
        }
        printf("\r\n");
    }
    /* first 32 bytes */
    printf("  bytes[0..31]: ");
    for (uint32_t i = 0; i < 32; i++) printf("%02x ", (unsigned)snap_buf[i]);
    printf("\r\n");

    /* 3) run-length scan of row 0: print each run of ~constant words as a
     * bar segment (start x, end x, width, word). This reveals the TRUE
     * bar period and where it wraps (the trailing-white bar). */
    printf("row0 bar segments (run-length of identical word):\r\n");
    {
        uint32_t seg_x = 0;
        uint16_t seg_w = 0xFFFF;
        for (uint32_t x = 0; x <= img_width; x++)
        {
            uint16_t w;
            if (x < img_width)
            {
                const uint8_t *px = snap_buf + (size_t)x * 2u;
                w = (uint16_t)(px[0] | ((uint16_t)px[1] << 8));
            }
            else
            {
                w = 0xFFFF;       /* force a flush at row end */
            }
            if (w != seg_w)
            {
                if (x > seg_x)
                {
                    printf("  [%4lu..%4lu] w=%04x  (r%d g%d b%d)\r\n",
                           (unsigned long)seg_x, (unsigned long)(x - 1),
                           (unsigned)seg_w,
                           (unsigned)((seg_w >> 11) & 31u),
                           (unsigned)((seg_w >> 5) & 63u),
                           (unsigned)(seg_w & 31u));
                }
                seg_x = x;
                seg_w = w;
            }
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
    LcdCamera_AsciiString(0, 2, "Mode:VGA 640x480 1:1", COL_WHITE, COL_TRANSPARENT, 3);
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

    /* DCMI VGA capture: starts the continuous 4-quarter ring into snap_buf. */
    OV7670_Init();

    /* ------------------------------------------------------------------
     * Boot demo: sensor built-in 8-color test pattern - freeze ONE frame
     * at a VSYNC-aligned moment. The continuous ring's DMA write phase is
     * arbitrary, so stopping it mid-frame gave a rotated/partially-wrapped
     * bar set (phase changes every boot -> the "fast-changing offset").
     * Here we wait for the DCMI FRAME event (= a complete sensor frame just
     * wrote Q0..Q3), then stop: the buffer then holds a complete frame with
     * row 0 = sensor row 0, so the bars are always in the same place. */
    printf("test pattern ON (VSYNC-aligned freeze)\r\n");
    sensor_test_bar(1);
    {
        uint32_t t0 = HAL_GetTick();
        while (!OV7670_FrameState)      /* wait for a complete frame */
        {
            if ((HAL_GetTick() - t0) > 2000U) break;   /* timeout guard */
        }
        OV7670_FrameState = 0;
        OV7670_CaptureStop();           /* stop right after frame end */
        printf("capture frozen at frame boundary\r\n");
    }
    ov7670_dump_capture();
    OV7670_CaptureStop();

    /* Display ONE frozen pattern frame. First try the normalized pattern
     * renderer (anchors on the white bar, resamples to exact 80-px bars -
     * no overlaps, no trailing sliver); if that fails, fall back to the
     * raw 1:1 blit. */
    LcdCamera_CameraFill(FB0, 0, 0, LCD_WIDTH, LCD_HEIGHT, 0x001FU);
    if (render_pattern(FB0))
    {
        printf("pattern: rendered normalized 8 bars\r\n");
    }
    else
    {
        printf("pattern: white bar not found - raw blit\r\n");
        blit_snap(FB0);
    }
    LcdCamera_SetLayer0FB(FB0);
    printf("static pattern held on LCD (left edge). Describe what you see.\r\n");
    while (1)
    {
    }

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