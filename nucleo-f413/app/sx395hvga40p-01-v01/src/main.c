/*
  sx395hvga40p-01-v01 main for the nucleo-f413 (STM32F413ZHT6 @ 100 MHz).
  SX395HVGA40P-01-V01 module: ILI9488 3.5" 320x480 (windows at
  COL_Pre = ROW_Pre = 0, MADCTL 0x08, 16bpp), 4-wire SPI on both drive
  methods (soft bit-bang fallback / HW SPI1 @ 50 MHz default), plus backlight
  PWM on PE9.

  Pattern set, on BOTH drive methods:
    - big-font banner page (8x16 font), then the full pattern set:
      info page (normal + inverted), TEST_STAND screens,
      HSV gradient sweep, LED test - with a live FPS counter throughout;
    - NES-size pass (centered 256x224 window) on both buses.

  Wiring: SCL=PA5, SDA=PA7, DC=PD15, RST=PF12, CS=PD14, BL=PE9
  (TIM1_CH1 PWM). No touch wiring on this
  module's connector as used here.

  Clock: the F413 board tree runs APB2 at 100 MHz. SPI1 defaults to
  12.5 MHz with prescaler /8; /4 = 25 MHz and /2 = 50 MHz via
  LCD_SPI1_PRESC if the module proves it.
*/

#include <stdio.h>
#include <string.h>
#include "board.h"
#include "lcd.h"
#include "interface.h"
#include "lcd/lcd_font_1608.h"
#include "backlight.h"

#define SCREEN_W   LCD_Width     /* 320 max (buffer sizing) */
#define SCREEN_H   LCD_Height    /* 480 */
#define FPS_BAND   20            /* bottom rows reserved for the FPS text   */
#define BACK_COLOR LCD_BLACK
#define LED_HALF   1000          /* LED test dwell (ms)                    */

/* Runtime window geometry (follows LCD_SetWindow). */
static uint16_t anim_h(void)
{
    return (uint16_t)(LCD_H() - FPS_BAND);
}

/* The 320 px panel fits 53 chars/line with the 6x12 font; the info page
 * keeps compact 21-char lines anyway. */
#define INFO_DY    13

/* --------------------------------------------------------------------- */
/* FPS counter.                                                          */
static volatile uint32_t g_frames;
static uint32_t         g_last_frames;
static uint32_t         g_fps_last_tick;
static uint32_t         g_fps_color = LCD_WHITE;   /* FPS glyph color     */
static uint8_t          g_panel_id[3];

static void fps_frame(void)
{
    g_frames++;
}

static void fps_update(void)
{
    uint32_t now = HAL_GetTick();
    if (now - g_fps_last_tick >= 1000)
    {
        uint32_t fps = g_frames - g_last_frames;
        g_last_frames = g_frames;
        g_fps_last_tick = now;

        char buf[8];
        buf[0] = 'F'; buf[1] = 'P'; buf[2] = 'S'; buf[3] = ':';
        buf[4] = (char)('0' + (fps / 100) % 10);
        buf[5] = (char)('0' + (fps / 10) % 10);
        buf[6] = (char)('0' + fps % 10);
        buf[7] = '\0';
        LCD_SetColor(g_fps_color);
        LCD_ShowTransparent(1);              /* no opaque box */
        LCD_DisplayString(1, anim_h() + 4, buf);
        LCD_ShowTransparent(0);
    }
}

static void paint_fps_band(void)
{
    LCD_SetColor(BACK_COLOR);
    LCD_SetBackColor(BACK_COLOR);
    LCD_FillRect(0, anim_h(), LCD_W(), FPS_BAND);
}

static void delay_with_fps(uint32_t ms)
{
    uint32_t start = HAL_GetTick();
    do
    {
        fps_update();
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

static void draw_gradient(int hue_a, int hue_b, uint16_t *row)
{
    for (int y = 0; y < anim_h(); y++)
    {
        int frac = y * 1000 / anim_h();
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
    paint_fps_band();

    uint32_t start = HAL_GetTick();
    uint32_t t = 0;
    do
    {
        int hue_a = (int)(t * 3600 / ms);
        int hue_b = hue_a + 1800;
        if (hue_b >= 3600) { hue_b -= 3600; }
        draw_gradient(hue_a, hue_b, row);
        fps_frame();
        fps_update();
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
/* Vendor TEST_STAND screens (320x480). The five solid-color fills are
 * timed (ms) - the durations belong to the current driving method only:
 * run_patterns() resets them each pass, before the fills run.          */
static uint32_t g_solid_ms[5];
static const char *const g_solid_name[5] =
{
    "RED", "GREEN", "BLUE", "WHITE", "BLACK"
};

/* Draw the pattern name on the panel (top-left, 2x font) so the color
 * can be verified against the label. Text color picked for contrast. */
static void draw_pattern_label(const char *name, uint32_t pattern_565,
                               int light_background)
{
    LCD_SetColor(light_background ? LCD_BLACK : LCD_WHITE);
    LCD_SetBackColor(pattern_565);
    LCD_DisplayStringScaled(4, 4, (char *)name, 2);
}

static void TEST_STAND(void)
{
    const uint32_t solid_color[5] = { RED, GREEN, BLUE, WHITE, BLACK };
    const char *const solid_lbl[5] = { "RED", "GREEN", "BLUE", "WHITE", "BLACK" };

    printf("[LCD] pattern: Frame\r\n");
    DispFrame();
    draw_pattern_label("Frame", 0x0000U, 0);
    StopDelay(Delay_Time);

    printf("[LCD] pattern: GrayHor16\r\n");
    DispGrayHor16();
    draw_pattern_label("Gray16", 0x0000U, 0);
    StopDelay(Delay_Time);

    printf("[LCD] pattern: Band\r\n");
    DispBand();
    draw_pattern_label("Band", 0x0000U, 0);
    StopDelay(Delay_Time);

    for (int i = 0; i < 5; i++)
    {
        uint32_t t0 = HAL_GetTick();
        printf("[LCD] pattern: solid %s\r\n", solid_lbl[i]);
        DispColor(solid_color[i]);
        g_solid_ms[i] = HAL_GetTick() - t0;
        /* white/black fills get a black label; the rest get white */
        draw_pattern_label(solid_lbl[i], solid_color[i],
                           (i == 3 || i == 4) ? 1 : 0);
        StopDelay(Delay_Time);
    }

    printf("[LCD] solid fills (ms): RED=%lu GREEN=%lu BLUE=%lu "
           "WHITE=%lu BLACK=%lu\r\n",
           (unsigned long)g_solid_ms[0], (unsigned long)g_solid_ms[1],
           (unsigned long)g_solid_ms[2], (unsigned long)g_solid_ms[3],
           (unsigned long)g_solid_ms[4]);
}

static void lcd_spi_read_diagnostics(void)
{
    /* Reads return 0x00 on this module: the ILI9488 4-line serial
     * interface does not support read-back (the panel never drives its
     * SDO line in 4-line mode). Kept as documentation; not called. */
}

/* --------------------------------------------------------------------- */
/* Info page: compiler, build date, current frequency, drive method, the
   IO map, backlight duty, the MCU unique device ID and the solid-color
   fill durations of the current pass. `invert` swaps fg/bg (white
   background page).                                                     */
static void info_demo(uint32_t ms, uint8_t invert)
{
    char buf[24];
    char comp[24];
    const uint32_t *uid = (const uint32_t *)UID_BASE;
    unsigned long mhz = (unsigned long)(SystemCoreClock / 1000000UL);
    uint16_t duty = Backlight_GetDuty();
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

    printf("[LCD] info%s: compiler=%s build=%s %s\r\n",
           invert ? " (inverted)" : "", comp, __DATE__, __TIME__);
    printf("[LCD] info: freq=%lu MHz drive=%s BL=%u%%\r\n",
           mhz, LCD_BusIsHw() ? "HW SPI1" : "soft bit-bang", (unsigned)duty);
    printf("[LCD] info: UID=%08lX%08lX%08lX\r\n",
           (unsigned long)uid[0], (unsigned long)uid[1], (unsigned long)uid[2]);

    LCD_SetColor(fg);
    LCD_SetBackColor(bg);
    LCD_Clear();

    /* FPS band in the page background color, then restore fg/bg. */
    LCD_SetColor(bg);
    LCD_SetBackColor(bg);
    LCD_FillRect(0, anim_h(), LCD_W(), FPS_BAND);
    LCD_SetColor(fg);
    LCD_SetBackColor(bg);
    g_fps_color = fg;                     /* FPS glyph matches the page */

    /* Scaled 2x text (8x16 font -> 16x32 glyphs): larger, and only the
     * compact line set fits the 320 px width. y starts below the top
     * edge margin; the FPS band stays at the bottom. */
    const int ix = 2;
    const int dy = 36;
    int y = 6;

    snprintf(buf, sizeof buf, "%s / %.8s", comp, __DATE__);
    LCD_DisplayStringScaled((uint16_t)ix, (uint16_t)y, buf, 2);  y += dy;

    snprintf(buf, sizeof buf, "Freq %lu MHz", mhz);
    LCD_DisplayStringScaled((uint16_t)ix, (uint16_t)y, buf, 2);  y += dy;

    if (LCD_BusIsHw())
    {
        snprintf(buf, sizeof buf, "HW SPI1 %lu MHz",
                 LCD_HwSpiKHz() / 1000UL);
    }
    else
    {
        snprintf(buf, sizeof buf, "Bus soft (bit-bang)");
    }
    LCD_DisplayStringScaled((uint16_t)ix, (uint16_t)y, buf, 2);  y += dy;

    snprintf(buf, sizeof buf, "SCL=PA5 SDA=PA7");
    LCD_DisplayStringScaled((uint16_t)ix, (uint16_t)y, buf, 2);  y += dy;

    snprintf(buf, sizeof buf, "RST=PF12 DC=PD15");
    LCD_DisplayStringScaled((uint16_t)ix, (uint16_t)y, buf, 2);  y += dy;

    snprintf(buf, sizeof buf, "CS=PD14 BL=PE9 %u%%", (unsigned)duty);
    LCD_DisplayStringScaled((uint16_t)ix, (uint16_t)y, buf, 2);  y += dy;

    snprintf(buf, sizeof buf, "UID %08lX", (unsigned long)uid[0]);
    LCD_DisplayStringScaled((uint16_t)ix, (uint16_t)y, buf, 2);  y += dy;

    /* Solid-color fill durations, measured earlier in this pass (the
     * TEST_STAND solids run before the info pages) - so they always
     * reflect the current driving method. */
    for (int i = 0; i < 5; i++)
    {
        snprintf(buf, sizeof buf, "%-5s : %4lu ms",
                 g_solid_name[i], (unsigned long)g_solid_ms[i]);
        LCD_DisplayStringScaled((uint16_t)ix, (uint16_t)y, buf, 2);  y += dy;
    }

    g_fps_color = LCD_WHITE;              /* restore default FPS glyph color */

    uint32_t start = HAL_GetTick();
    do
    {
        fps_update();
        HAL_Delay(50);
    } while (HAL_GetTick() - start < ms);
}

/* --------------------------------------------------------------------- */
/* Big-font banner page (8x16 font), lines centered on the panel width.  */
static void banner_page(const char *l1, const char *l2,
                        uint32_t fg, uint32_t bg, uint32_t ms)
{
    g_fps_color = fg;

    LCD_SetAsciiFont(&ASCII_Font16);      /* bigger than the normal 6x12  */
    LCD_SetColor(fg);
    LCD_SetBackColor(bg);
    LCD_Clear();
    LCD_SetColor(bg);
    LCD_SetBackColor(bg);
    LCD_FillRect(0, anim_h(), LCD_W(), FPS_BAND);
    LCD_SetColor(fg);
    LCD_SetBackColor(bg);

    /* 8 px/glyph x scale 2 = 16 px/glyph: center each line dynamically.
     * Clamp to 0 for overlong lines (a negative would wrap the uint16_t
     * x and the whole line would silently vanish off-screen). */
    {
        int tx1 = (int)LCD_W() - (int)strlen(l1) * 16;
        int tx2 = (int)LCD_W() - (int)strlen(l2) * 16;
        LCD_DisplayStringScaled((uint16_t)(tx1 > 0 ? tx1 / 2 : 0), 40,
                                (char *)l1, 2);
        LCD_DisplayStringScaled((uint16_t)(tx2 > 0 ? tx2 / 2 : 0), 90,
                                (char *)l2, 2);
    }

    HAL_Delay(ms);

    LCD_SetAsciiFont(&ASCII_Font16);      /* stay on the 8x16 font        */
    g_fps_color = LCD_WHITE;
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
    info_demo(5000, 0);

    printf("[LCD] phase: info (inverted colors)\r\n");
    info_demo(5000, 1);

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

    printf("\r\n==== nucleo-f413 (STM32F413ZHT6) sx395hvga40p-01-v01 @ %lu MHz ====\r\n",
           (unsigned long)(SystemCoreClock / 1000000UL));
    printf("ILI9488 3.5\" 320x480 (MADCTL 0x08), "
           "4-wire SPI, HW SPI1 @ 50 MHz (SPI1 max; 12.5 MHz garbles - "
           "see README):\r\n");
    printf("SCL=PA5 SDA=PA7 DC=PD15 RST=PF12 CS=PD14 BL=PE9(PWM 15%%) "
           "MISO=PA6\r\n");

    Backlight_Init();
    Backlight_SetDuty(50U);
    LCD_Init();

    LCD_UseHwBus();
    LCD_Reinit();
    /* NOTE: panel register reads are not available on this module: the
     * ILI9488 4-line serial interface does not support read-back (SDO
     * is never driven - every read returns 0x00). LCD_ReadBytes() stays
     * in interface.c for a future 3-line-mode experiment. */

    LCD_UseSoftBus();
    LCD_Reinit();
    LCD_SetAsciiFont(&ASCII_Font16);
    paint_fps_band();

    /* -----------------------------------------------------------------
     * Dual-bus demo loop (st7365_md350 behavior): SOFT pass then
     * HARDWARE pass, full-size patterns only (no NES window).
     * Backlight = 50% PWM. LCD_Reinit() re-frames the panel each pass.
     * ----------------------------------------------------------------- */
    LCD_SetAsciiFont(&ASCII_Font16);
    paint_fps_band();

    char l1[32] = "SX395 320x480";
    char l2[40];

    while (1)
    {
        /* ---- SOFT pass: fastest bit-bang (pace 0 ~ 5-7 MHz), 15% BL ---- */
        LCD_SoftSetPace(0U);
        LCD_UseSoftBus();
        Backlight_SetDuty(15U);
        LCD_Reinit();
        LCD_ResetWindow();

        snprintf(l2, sizeof l2, "SOFT SPI BL 15%%");
        printf("[LCD] phase: SOFT banner (%s)\r\n", l2);
        banner_page(l1, l2, LCD_YELLOW, LCD_BLUE, 3000);

        printf("[LCD] running patterns on SOFT SPI (320x480)\r\n");
        run_patterns();

        /* ---- HARDWARE pass: SPI1 at its 50 MHz maximum, 30% BL ---- */
        LCD_UseHwBus();
        LCD_HwSetPrescaler(SPI_BAUDRATEPRESCALER_2);
        Backlight_SetDuty(30U);
        LCD_Reinit();
        LCD_ResetWindow();

        snprintf(l2, sizeof l2, "HW 50 MHz BL 30%%");
        printf("[LCD] phase: HARDWARE banner (%s)\r\n", l2);
        banner_page(l1, l2, LCD_BLACK, LCD_CYAN, 3000);

        printf("[LCD] running patterns on HARDWARE SPI1 @ 50 MHz (320x480)\r\n");
        run_patterns();
    }

    return 0;
}
