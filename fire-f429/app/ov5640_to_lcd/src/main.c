/**
  * @file    ov5640_to_lcd/src/main.c
  * @brief   OV5640 camera -> 5" 800x480 LCD on the fire-f429 board.
  *
  * Behavior mirrors the vendor 45-OV5640_sensor example (F429IG-V1V2):
  *   - the OV5640 is captured by DCMI + DMA and displayed live on the LCD,
  *   - an FPS / status line is overlaid and refreshed once per second,
  *   - on a missing camera the failure is printed on the LCD and the
  *     program stops (vendor behavior).
  *
  * Implementation notes (fire-f429 specific):
  *   - The camera driver is the proven one from app/eth_http_server: the
  *     module runs RGB565 QQVGA 160x120 (the stable mode on this
  *     24 MHz-crystal module - the built-in JPEG encoder does not produce
  *     valid output through the F4 DCMI).
  *   - Frames are nearest-neighbour stretched 5x horizontally / 4x
  *     vertically to fill the 800x480 RGB888 framebuffer (the board LCD
  *     driver uses a single RGB888 layer), then the buffer is swapped.
  *   - The status HUD (title + FPS line) is redrawn every frame over the
  *     camera image with a black background, like the vendor's overlay.
  *   - LED_G blinks while frames are flowing; LED_R is on if the camera
  *     fails to initialize.
  *
  * Pins (same as the vendor example, fire-f429 wiring):
  *   DCMI: VSYNC PI5, HSYNC PA4, PIXCLK PA6, D0..D3 PH9..12, D4 PH14,
  *         D5 PD3, D6 PI6, D7 PI7, PWDN PG3, RST PG2
  *   SCCB: I2C1 PB6 (SCL) / PB7 (SDA)
  */

#include "board.h"
#include "board_ltdc.h"
#include "ov5640.h"
#include <stdio.h>

#define SCREEN_W    LCD_WIDTH          /* 800 */
#define SCREEN_H    LCD_HEIGHT         /* 480 */

/* Camera -> LCD stretch factors (integer, nearest-neighbour). */
#define SCALE_X     5U                 /* 800 / 160 */
#define SCALE_Y     4U                 /* 480 / 120 */

#define COL_BLACK   0x00000000U
#define COL_WHITE   0x00FFFFFFU
#define COL_YELLOW  0x00FFFF00U
#define COL_RED     0x00FF0000U
#define COL_GREEN   0x0000FF00U
#define COL_BLUE    0x000000FFU
#define COL_DKGRAY  0x00203040U

/* FPS accounting: incremented once per displayed frame. */
static volatile uint32_t g_displayed;
static uint32_t          g_last_displayed;
static uint32_t          g_status_tick;
static char              g_status[64];

/* ------------------------------------------------------------------ */
/* Stretch-blit one RGB565 QQVGA frame into the LTDC back buffer as
 * RGB888, filling the whole 800x480 screen (5x H, 4x V).
 *
 * Each source row is converted + stretched once into a small SRAM staging
 * buffer, then the finished RGB888 row is repeated SCALE_Y times into the
 * SDRAM framebuffer with 32-bit copies (byte-wise SDRAM stores are ~4x
 * slower than word stores on the FMC). */

/* The staging row must live in internal SRAM (.sram_dma): plain .bss is
 * placed in SDRAM by stm32f429_sdram.ld, which would make the copy loop
 * read AND write through the FMC (SDRAM<->SDRAM, ~2x the traffic). */
static uint8_t row_rgb[SCREEN_W * 3U]
    __attribute__((section(".sram_dma"), aligned(4)));

static void camera_blit(const uint8_t *rgb565)
{
    volatile uint8_t *fb = (volatile uint8_t *)(uintptr_t)LTDC_Display_BackBuffer();

    for (uint32_t sy = 0; sy < OV5640_FRAME_H; sy++)
    {
        /* Convert + stretch source row `sy` into row_rgb (800 px RGB888). */
        const uint8_t *src = rgb565 + sy * (OV5640_FRAME_W * 2U);
        uint8_t *dst_row = row_rgb;
        for (uint32_t sx = 0; sx < OV5640_FRAME_W; sx++)
        {
            uint16_t c = (uint16_t)(src[sx * 2U] | (uint16_t)(src[sx * 2U + 1U] << 8));
            /* RGB565 -> RGB888 with bit replication. */
            uint32_t r5 = (c >> 11) & 0x1FU;
            uint32_t g6 = (c >> 5)  & 0x3FU;
            uint32_t b5 = c & 0x1FU;
            uint8_t r = (uint8_t)((r5 << 3) | (r5 >> 2));
            uint8_t g = (uint8_t)((g6 << 2) | (g6 >> 4));
            uint8_t b = (uint8_t)((b5 << 3) | (b5 >> 2));

            for (uint32_t k = 0; k < SCALE_X; k++)
            {
                *dst_row++ = r;
                *dst_row++ = g;
                *dst_row++ = b;
            }
        }

        /* Repeat the row SCALE_Y times into the framebuffer (word copy). */
        for (uint32_t rep = 0; rep < SCALE_Y; rep++)
        {
            volatile uint32_t *dst =
                (volatile uint32_t *)(fb + ((sy * SCALE_Y + rep) * SCREEN_W * 3U));
            const uint32_t *srcw = (const uint32_t *)row_rgb;
            for (uint32_t i = 0; i < (SCREEN_W * 3U) / 4U; i++)
            {
                dst[i] = srcw[i];
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* HUD: title + status line with black backgrounds (vendor overlay).  */

static void draw_hud(const char *title, const char *status)
{
    LTDC_DrawString(8, 4, title, COL_WHITE, COL_BLACK);
    LTDC_DrawString(8, 20, status, COL_YELLOW, COL_BLACK);
}

/* Refresh the FPS line once per second (vendor example does the same).
 * LED_1 blinks once per second; every 5 s the FPS is echoed to the console
 * so the display rate can be verified from the serial log. */
static void status_update(void)
{
    uint32_t now = HAL_GetTick();
    if ((now - g_status_tick) < 1000U)
    {
        return;
    }
    g_status_tick = now;
    LED_1_TOGGLE();

    uint32_t fps = g_displayed - g_last_displayed;
    g_last_displayed = g_displayed;

    snprintf(g_status, sizeof(g_status),
             "FPS:%03lu  frames:%lu  %ux%u RGB565 -> %ux%u RGB888",
             (unsigned long)fps,
             (unsigned long)OV5640_FrameCount(),
             (unsigned)OV5640_FRAME_W, (unsigned)OV5640_FRAME_H,
             (unsigned)SCREEN_W, (unsigned)SCREEN_H);

    static uint32_t log_count;
    if (++log_count >= 5U)
    {
        log_count = 0;
        printf("display: %lu fps, %lu frames, camera ready=%d\r\n",
               (unsigned long)fps,
               (unsigned long)OV5640_FrameCount(),
               OV5640_Ready());
    }
}

/* ------------------------------------------------------------------ */

static void show_fatal(const char *line1, const char *line2)
{
    LTDC_Clear(COL_DKGRAY);
    LTDC_Display_Swap();
    LTDC_Clear(COL_DKGRAY);
    LTDC_DrawString(60, 200, line1, COL_RED, COL_BLACK);
    LTDC_DrawString(60, 220, line2, COL_WHITE, COL_BLACK);
    LTDC_Display_Swap();
    printf("OV5640: %s - %s\r\n", line1, line2);
    LED_R_ON();
    while (1)
    {
        HAL_Delay(100);          /* stop here, like the vendor example */
    }
}

/* ------------------------------------------------------------------ */

int main(void)
{
    HAL_Init();
    Board_Init();                /* 180 MHz, LEDs, USART1 console, SDRAM */
    LTDC_Display_Init();         /* 5" 800x480 RGB888, double buffered */

    printf("\r\n=== ov5640_to_lcd on fire-f429 (OV5640 -> LTDC) ===\r\n");
    printf("LCD: %ux%u RGB888, camera: RGB565 %ux%u\r\n",
           (unsigned)SCREEN_W, (unsigned)SCREEN_H,
           (unsigned)OV5640_FRAME_W, (unsigned)OV5640_FRAME_H);

    /* Splash screen while the camera powers up. */
    LTDC_Clear(COL_BLACK);
    LTDC_Display_Swap();
    LTDC_Clear(COL_BLACK);
    draw_hud("OV5640 -> LCD  (fire-f429)",
             "camera init...");
    LTDC_Display_Swap();
    LED_G_ON();

    /* Init + verify the sensor (driver retries with power-cycles). */
    if (OV5640_Init() != 0)
    {
        show_fatal("OV5640 not detected (SCCB)",
                   "check camera cable, then reset");
    }

    /* Boot diagnostics: prints measured camera FPS to the console. */
    OV5640_Selftest();
    snprintf(g_status, sizeof(g_status), "FPS:---  starting...");
    g_status_tick = HAL_GetTick();

    printf("Live camera -> LCD. LED_G blinks per frame, LED_1 per second.\r\n");

    while (1)
    {
        const uint8_t *frame;
        if (!OV5640_GetLatestFrame(&frame))
        {
            HAL_Delay(1);
            continue;
        }

        camera_blit(frame);
        status_update();
        draw_hud("OV5640 -> LCD  (fire-f429)", g_status);
        LTDC_Display_Swap();
        g_displayed++;
        LED_G_TOGGLE();      /* blink per displayed frame */

        OV5640_HealthCheck();    /* stall watchdog (rate-limited) */
        HAL_Delay(1);
    }
}
