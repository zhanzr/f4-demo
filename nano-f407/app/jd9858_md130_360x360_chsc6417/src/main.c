/*
  jd9858_md130_360x360_chsc6417 main for the nano-f407
  (STM32F407VET6 @ 168 MHz). MD130 module: JD9858 (DS: JD5858) 360x360
  ROUND IPS panel on the FSMC bus (NE1 = CS, A16 = DC, NOE = RD,
  NWE = WR, D0..D7 - vendor F103VET6 wiring, identical on the F407VET6;
  vendor-verbatim init from STM32_TK0013F1327_LCD_hal_captouch), plus
  CHSC6417 capacitive touch (bit-banged I2C PB13/PB15) with raw touch
  data printed on the serial console.

  Pattern set (per pass, live FPS counter throughout):
    - big-font banner page (8x16 font), then the full pattern set:
      info page (normal + inverted), TEST_STAND screens,
      HSV gradient sweep, LED test.
    - touch polls run during the waits; raw touch data changes are
      printed on the serial port.

  Panel controls: RST = PD13, backlight = PA1 (switched on at init).
*/

#include <stdio.h>
#include <string.h>
#include "board.h"
#include "lcd.h"
#include "interface.h"
#include "touch.h"
#include "backlight.h"
#include "lcd/lcd_font_1608.h"

#define SCREEN_W   LCD_Width     /* 360 (row buffer sizing) */
#define FPS_BAND   20            /* bottom rows reserved for the FPS text   */
#define BACK_COLOR LCD_BLACK
#define LED_HALF   1000          /* LED test dwell (ms)                    */
#define INFO_DY    13            /* info page line pitch                   */

/* Runtime window geometry (follows LCD_SetWindow). */
static uint16_t anim_h(void)
{
    return (uint16_t)(LCD_H() - FPS_BAND);
}

/* --------------------------------------------------------------------- */
/* FPS counter.                                                          */
static volatile uint32_t g_frames;
static uint32_t         g_last_frames;
static uint32_t         g_fps_last_tick;

static void fps_frame(void)
{
    g_frames++;
}

static char     g_fps_text[8] = "FPS:000";

/* Draw the counter as an opaque red-on-white box on EVERY call: pages
 * and the gradient repaint continuously, so a once-per-second draw
 * would be erased before it is ever seen. Above the bottom curve of
 * the round glass, centered. */
static void fps_update(void)
{
    uint32_t now = HAL_GetTick();
    if (now - g_fps_last_tick >= 1000)
    {
        uint32_t fps = g_frames - g_last_frames;
        g_last_frames = g_frames;
        g_fps_last_tick = now;

        g_fps_text[4] = (char)('0' + (fps / 100) % 10);
        g_fps_text[5] = (char)('0' + (fps / 10) % 10);
        g_fps_text[6] = (char)('0' + fps % 10);
    }

    uint32_t fc = LCD_GetColor();
    uint32_t bc = LCD_GetBackColor();
    LCD_SetColor(0xFF0000U);             /* red glyphs      */
    LCD_SetBackColor(0xFFFFFFU);         /* on a white box  */
    LCD_ShowTransparent(0);              /* opaque box      */
    LCD_DisplayString((uint16_t)((LCD_W() - 7U * 8U) / 2U),
                      (uint16_t)(LCD_H() - 24U), g_fps_text);
    LCD_SetColor(fc);
    LCD_SetBackColor(bc);
}

static void paint_fps_band(void)
{
    LCD_SetColor(BACK_COLOR);
    LCD_SetBackColor(BACK_COLOR);
    LCD_FillRect(0, anim_h(), LCD_W(), FPS_BAND);
}

/* --------------------------------------------------------------------- */
/* Touch printout: polls the CHSC6417. Register 0x00 bits [2:0] hold
 * the touch-point count (0 = no touch, 1..5 valid; per the ESP32S3
 * vendor driver), bit6/bit7 are the X/Y MSBs of the 9-bit coords.
 *
 * Noise hardening: at 16.8 MHz the FSMC writes couple into the touch
 * FPC, so single reads occasionally return fake data. Every poll takes
 * two reads and only accepts them when they agree, then debounces:
 * 3 consecutive valid reads to register a touch, 6 consecutive idle
 * reads to register a release. */
static uint8_t s_touch_down;
static uint8_t s_down_cnt, s_up_cnt;
static uint16_t s_touch_lx = 0xFFFFU, s_touch_ly = 0xFFFFU;

static void touch_task(void)
{
    uint8_t a[4], b[4];
    uint16_t x, y;
    uint8_t status;

    Touch_Read(a, 4);
    Touch_Read(b, 4);
    if (memcmp(a, b, sizeof a) != 0)
    {
        return;                          /* reads disagree: bus glitch */
    }

    status = (uint8_t)(a[0] & 0x07U);
    x = (uint16_t)((((a[0] & 0x40U) >> 6) << 8) | a[1]);
    y = (uint16_t)((((a[0] & 0x80U) >> 7) << 8) | a[2]);

    if (status != 0U && status <= 5U)
    {
        if (s_down_cnt < 255U) { s_down_cnt++; }
        s_up_cnt = 0U;

        if (s_touch_down == 0U)
        {
            if (s_down_cnt >= 3U)
            {
                printf("[TOUCH] down X=%u Y=%u (points=%u)\r\n",
                       (unsigned)x, (unsigned)y, (unsigned)status);
                s_touch_down = 1U;
                s_touch_lx = x;
                s_touch_ly = y;
            }
        }
        else if (x != s_touch_lx || y != s_touch_ly)
        {
            printf("[TOUCH] move X=%u Y=%u\r\n", (unsigned)x, (unsigned)y);
            s_touch_lx = x;
            s_touch_ly = y;
        }
    }
    else
    {
        if (s_up_cnt < 255U) { s_up_cnt++; }
        s_down_cnt = 0U;

        if (s_touch_down != 0U && s_up_cnt >= 6U)
        {
            printf("[TOUCH] release\r\n");
            s_touch_down = 0U;
            s_touch_lx = 0xFFFFU;
            s_touch_ly = 0xFFFFU;
        }
    }
}

static void delay_with_fps(uint32_t ms)
{
    uint32_t start = HAL_GetTick();
    do
    {
        fps_update();
        touch_task();                        /* touch printout during waits */
        HAL_Delay(50);
    } while (HAL_GetTick() - start < ms);
}

/* --------------------------------------------------------------------- */
/* Animated gradient: hue sweeps the full color wheel over `ms`.         */
static uint32_t hsv_to_rgb(int h, int s, int v)
{
    int region = (h / 600) % 6;
    int fpart  = h % 600;
    int p = v * (255 - s) / 255;
    int q = v * (255 - (s * fpart) / 600) / 255;
    int t = v * (255 - (s * (600 - fpart)) / 600) / 255;
    int r, g, b;
    switch (region)
    {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default:r = v; g = p; b = q; break;
    }
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* Full-height gradient: covers the FPS band too (the FPS glyphs are
 * drawn transparently on top of it), so no dark strip is left. */
static void draw_gradient(int hue_a, int hue_b, uint16_t *row)
{
    int h0 = (int)LCD_H();
    for (int y = 0; y < h0; y++)
    {
        int frac = y * 1000 / h0;
        int hue  = hue_a + (hue_b - hue_a) * frac / 1000;
        uint32_t c = hsv_to_rgb(hue, 255, 255);
        uint16_t rgb565 = (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) |
                                     ((c >> 3) & 0x001F));
        for (int x = 0; x < LCD_W(); x++)
        {
            row[x] = rgb565;
        }
        LCD_CopyBuffer(0, (uint16_t)y, LCD_W(), 1, row);
    }
}

static void gradient_demo(uint32_t ms)
{
    static uint16_t row[SCREEN_W];
    LCD_SetBackColor(BACK_COLOR);

    uint32_t start = HAL_GetTick();
    uint32_t t = 0;
    do
    {
        int hue_a = (int)(t * 3600 / ms);
        int hue_b = hue_a + 1800;
        if (hue_b >= 3600) { hue_b -= 3600; }
        draw_gradient(hue_a, hue_b, row);
        fps_frame();
        fps_update();   /* after the frame: the gradient would wipe it */
        t = HAL_GetTick() - start;
    } while (t < ms);
}

/* --------------------------------------------------------------------- */
/* LED test (PC13, low active).                                          */
static void led_test(void)
{
    printf("[LCD] LED ON\r\n");
    LED_ON();
    delay_with_fps(LED_HALF);
    printf("[LCD] LED OFF\r\n");
    LED_OFF();
    delay_with_fps(LED_HALF);
}

/* --------------------------------------------------------------------- */
/* Vendor TEST_STAND screens (360x360 round). The five solid-color fills are
 * timed (ms) - the durations belong to the current driving method only:
 * run_patterns() resets them each pass, before the fills run.          */
static uint32_t g_solid_ms[5];
static const char *const g_solid_name[5] =
{
    "RED", "GREEN", "BLUE", "WHITE", "BLACK"
};

static void TEST_STAND(void)
{
    const uint32_t solid_color[5] = { RED, GREEN, BLUE, WHITE, BLACK };

    DispFrame();
    StopDelay(Delay_Time);

    DispGrayHor16();
    StopDelay(Delay_Time);

    DispBand();
    StopDelay(Delay_Time);

    for (int i = 0; i < 5; i++)
    {
        uint32_t t0 = HAL_GetTick();
        DispColor(solid_color[i]);
        g_solid_ms[i] = HAL_GetTick() - t0;
        StopDelay(Delay_Time);
    }

    printf("[LCD] solid fills (ms): RED=%lu GREEN=%lu BLUE=%lu "
           "WHITE=%lu BLACK=%lu\r\n",
           (unsigned long)g_solid_ms[0], (unsigned long)g_solid_ms[1],
           (unsigned long)g_solid_ms[2], (unsigned long)g_solid_ms[3],
           (unsigned long)g_solid_ms[4]);
}

/* ---------------------------------------------------------------------
 * Info pages. Two variants share the content: `big` = 0 draws the small
 * 6x12 font (more rows), `big` = 1 draws the same 8x16 font the banner
 * uses (fewer rows, merged content to save the round surface's display
 * space). `invert` swaps fg/bg. The FSMC write rate is shown.
 *
 * Round surface: every line is centered horizontally. */

/* kHz -> "xx.x MHz" text: at most one digit after the dot, none if 0. */
static void fmt_mhz(char *dst, size_t n, unsigned long khz)
{
    unsigned long i = khz / 1000UL;
    unsigned long f = (khz % 1000UL) / 100UL;
    if (f != 0UL)
    {
        snprintf(dst, n, "%lu.%lu MHz", i, f);
    }
    else
    {
        snprintf(dst, n, "%lu MHz", i);
    }
}

/* center each info line horizontally (cw = glyph advance) */
static uint16_t center_x(const char *t, uint16_t cw)
{
    uint16_t w = (uint16_t)((uint16_t)strlen(t) * cw);
    return (uint16_t)((LCD_W() > w) ? ((LCD_W() - w) / 2U) : 0U);
}

/* Single info page, drawn with the module's smallest font (8x16 - the
 * 6x12 font garbles on this panel and is not used here). `invert`
 * swaps fg/bg. The FSMC write rate is shown as "xx.x MHz". */
static void info_page(uint32_t ms, uint8_t invert)
{
    char buf[40];
    char mhz_txt[16];
    char comp[24];
    const uint32_t *uid = (const uint32_t *)UID_BASE;
    unsigned long mhz = (unsigned long)(SystemCoreClock / 1000000UL);
    uint32_t fg = invert ? LCD_BLACK : LCD_WHITE;
    uint32_t bg = invert ? LCD_WHITE : LCD_BLACK;

#if defined(__ARMCC_VERSION)
    snprintf(comp, sizeof comp, "AC6 %lu", (unsigned long)__ARMCC_VERSION);
#elif defined(__clang__)
    snprintf(comp, sizeof comp, "Clang %d.%d", __clang_major__, __clang_minor__);
#elif defined(__GNUC__)
    snprintf(comp, sizeof comp, "GCC %d.%d.%d",
             __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
    snprintf(comp, sizeof comp, "unknown");
#endif

    fmt_mhz(mhz_txt, sizeof mhz_txt, LCD_FsmcKHz());

    printf("[LCD] info%s: compiler=%s build=%s %s\r\n",
           invert ? " (inverted)" : "", comp, __DATE__, __TIME__);
    printf("[LCD] info: freq=%lu MHz bus=FSMC %s\r\n", mhz, mhz_txt);
    printf("[LCD] info: UID=%08lX%08lX%08lX\r\n",
           (unsigned long)uid[0], (unsigned long)uid[1], (unsigned long)uid[2]);

    LCD_SetAsciiFont(&ASCII_Font16);
    LCD_SetColor(fg);
    LCD_SetBackColor(bg);
    LCD_Clear();

    /* Full-screen pass: drop the block a couple of rows - the circle
     * crops the first rows near the top edge. The narrow NES window
     * does not have that problem and keeps the top start. */
    int y = (LCD_H() >= 340U) ? 28 : 2;
    const uint16_t dy = 20U;

    snprintf(buf, sizeof buf, "%s / %.8s", comp, __DATE__);
    LCD_DisplayString(center_x(buf, 8U), (uint16_t)y, buf);  y += dy;

    snprintf(buf, sizeof buf, "%lu MHz FSMC %s", mhz, mhz_txt);
    LCD_DisplayString(center_x(buf, 8U), (uint16_t)y, buf);  y += dy;

    snprintf(buf, sizeof buf, "BL %u%% PWM TIM2_CH2", Backlight_GetDuty());
    LCD_DisplayString(center_x(buf, 8U), (uint16_t)y, buf);  y += dy;

    snprintf(buf, sizeof buf, "RST=PD13 TOUCH PB13/PB15");
    LCD_DisplayString(center_x(buf, 8U), (uint16_t)y, buf);  y += dy;

    snprintf(buf, sizeof buf, "UID %08lX", (unsigned long)uid[0]);
    LCD_DisplayString(center_x(buf, 8U), (uint16_t)y, buf);  y += dy;

    /* Solid-color fill durations, measured earlier in this pass (the
     * TEST_STAND solids run before the info pages) - so they always
     * reflect the current driving method. */
    for (int i = 0; i < 5; i++)
    {
        snprintf(buf, sizeof buf, "%-5s: %4lu ms",
                 g_solid_name[i], (unsigned long)g_solid_ms[i]);
        LCD_DisplayString(center_x(buf, 8U), (uint16_t)y, buf);  y += dy;
    }

    uint32_t start = HAL_GetTick();
    do
    {
        fps_update();
        touch_task();                     /* touch printout during waits */
        HAL_Delay(50);
    } while (HAL_GetTick() - start < ms);
}

/* --------------------------------------------------------------------- */
/* Big-font banner page (8x16 font), lines centered on the panel width.  */
static void banner_page(const char *l1, const char *l2,
                        uint32_t fg, uint32_t bg, uint32_t ms)
{
    LCD_SetAsciiFont(&ASCII_Font16);
    LCD_SetColor(fg);
    LCD_SetBackColor(bg);
    LCD_Clear();
    LCD_SetColor(bg);
    LCD_SetBackColor(bg);
    LCD_FillRect(0, anim_h(), LCD_W(), FPS_BAND);
    LCD_SetColor(fg);
    LCD_SetBackColor(bg);

    /* 8 px/glyph: center each line dynamically. */
    LCD_DisplayString((uint16_t)((LCD_W() - (int)strlen(l1) * 8) / 2), 40,
                      (char *)l1);
    LCD_DisplayString((uint16_t)((LCD_W() - (int)strlen(l2) * 8) / 2), 64,
                      (char *)l2);

    HAL_Delay(ms);
}

/* --------------------------------------------------------------------- */
/* Full test-pattern set. The solid-color fills run FIRST (they are timed
 * for the current driving method), then the two info pages display the
 * measured durations.                                                   */
static void run_patterns(void)
{
    printf("[LCD] phase: TEST_STAND\r\n");
    memset(g_solid_ms, 0, sizeof g_solid_ms);   /* current method only */
    TEST_STAND();

    printf("[LCD] phase: info\r\n");
    info_page(3500, 0);

    printf("[LCD] phase: info (inverted colors)\r\n");
    info_page(3500, 1);

    printf("[LCD] phase: gradient\r\n");
    gradient_demo(4000);

    printf("[LCD] phase: LED test\r\n");
    led_test();
}

/* --------------------------------------------------------------------- */
int main(void)
{
    HAL_Init();
    Board_Init();

    printf("\r\n==== nano-f407 (STM32F407VET6) jd9858_md130_360x360_chsc6417 @ %lu MHz ====\r\n",
           (unsigned long)(SystemCoreClock / 1000000UL));
    printf("JD9858 1.3\" 360x360 round (FSMC 8-bit, MADCTL 0xC0):\r\n");
    printf("NE1=PD7 A16=PD11(DC) NOE=PD4 NWE=PD5 D0..D7=PD14,15,0,1 PE7..10\r\n");
    printf("RST=PD13 BL=PA1; TOUCH: CHSC6417 I2C SCL=PB13 SDA=PB15\r\n");

    Backlight_Init();
    Backlight_SetDuty(15U);      /* round panel is bright: 15% default */
    printf("BL: TIM2_CH2 on PA1, PWM 1 kHz, default 15%%\r\n");

    Touch_Init();
    LCD_Init();
    LCD_SetAsciiFont(&ASCII_Font16);
    paint_fps_band();

    /* FSMC DATAST = 4 (16.8 MHz write) is fixed in interface.c - the
     * verified maximum clean rate for this module. */
    while (1)
    {
        printf("[LCD] phase: banner\r\n");
        LCD_Reinit();         /* re-frame the panel */
        banner_page("JD9858", "FSMC test",
                    LCD_BLACK, LCD_CYAN, 3000);

        printf("[LCD] running patterns (FSMC 8-bit bus)\r\n");
        run_patterns();

        /* ---- NES-size (256x224) centered window ---- */
        LCD_SetWindow(52U, 68U, 256U, 224U);
        banner_page("NES 256x224", "FSMC test",
                    LCD_BLACK, LCD_YELLOW, 3000);
        printf("[LCD] running NES-size patterns (FSMC)\r\n");
        run_patterns();
        LCD_ResetWindow();
    }

    return 0;
}
