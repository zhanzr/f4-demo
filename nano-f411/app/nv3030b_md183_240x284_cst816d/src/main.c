/*
  nv3030b_md183_240x284_cst816d main for the nano-f411
  (STM32F411CEU6 @ 100 MHz). MD183 module: NV3030B 240x284 IPS driven
  over its QSPI-compatible single-lane wrapped-command protocol
  (vendor-verbatim init), on the HARDWARE SPI1 bus @ 12.5 MHz
  (isolation rate; 25/50 MHz available via LCD_SPI1_PRESC), plus
  CST816D capacitive touch (I2C PA2/PA3) with the touch state printed
  on the serial console.

  Pattern set (per pass, live FPS counter throughout):
    - big-font banner page (8x16 font), then the full pattern set:
      info page (normal + inverted), TEST_STAND screens,
      HSV gradient sweep, LED test.
    - touch polls run during the waits; touches are printed on the
      serial port.

  Wiring: SCL=PA5, SDA/MOSI=PA7, CS=PA4. DC=PA6 per the vendor pin
  list is NOT used (the wrapped framing carries command/data; the
  vendor example defines it as DC and never drives it either). The
  module has no reset pin (the vendor settles CS instead) and no
  backlight control pin (hardwired on-module).

  NOTE: power-cycle the MODULE (unplug/replug its power, not just
  NRST) before judging a fix - the panel has no reset pin, so a
  latched bad state survives MCU resets.
*/

#include <stdio.h>
#include <string.h>
#include "board.h"
#include "lcd.h"
#include "interface.h"
#include "touch.h"
#include "lcd/lcd_font_1608.h"

#define SCREEN_W   LCD_Width     /* 240 (row buffer sizing) */
#define FPS_BAND   20            /* bottom rows reserved for the FPS text   */
#define BACK_COLOR LCD_BLACK
#define LED_HALF   1000          /* LED test dwell (ms)                    */
#define INFO_DY    13            /* info page line pitch                   */

/* ---- clock override: APB2 = 100 MHz (HCLK/1) for the 50 MHz SPI1 ---- */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM       = 25U;
    RCC_OscInitStruct.PLL.PLLN       = 200U;
    RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ       = 4U;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;   /* 50 MHz (max)   */
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;   /* 100 MHz (max)  */
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
    {
        Error_Handler();
    }
}

/* --------------------------------------------------------------------- */
/* Touch printout: polls the CST816D and prints state/X/Y on the serial
 * port (on touch-down, and on release).                                 */
static uint8_t s_touch_down;

static void touch_task(void)
{
    uint8_t buf[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    uint16_t x, y;

    Touch_Read(buf, 8);

    if (buf[3] == 0x80U && buf[4] > 1U)
    {
        x = buf[4];
        y = (uint16_t)(((buf[5] & 0x0FU) << 8) | buf[6]);

        if (s_touch_down == 0U)
        {
            printf("[TOUCH] down X=%u Y=%u (284-Y=%u)\r\n",
                   (unsigned)x, (unsigned)y, (unsigned)(284U - y));
            s_touch_down = 1U;
        }
    }
    else if (s_touch_down != 0U)
    {
        printf("[TOUCH] release\r\n");
        s_touch_down = 0U;
    }
}

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
static uint32_t         g_fps_color = LCD_WHITE;   /* FPS glyph color     */

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
/* Vendor TEST_STAND screens (240x284). The five solid-color fills are
 * timed (ms) - the durations belong to the current driving method only:
 * run_patterns() resets them each pass, before the fills run.          */
static uint32_t g_solid_ms[5];
static const char *const g_solid_name[5] =
{
    "RED", "GREEN", "BLUE", "WHITE", "BLACK"
};

/* kHz -> "2.1 MHz" / "50 MHz" text (shared by console + info page). */
static const char *mhz_text(unsigned long khz)
{
    static char t[16];
    if (khz % 1000UL == 0UL)
    {
        snprintf(t, sizeof t, "%lu MHz", khz / 1000UL);
    }
    else
    {
        snprintf(t, sizeof t, "%lu.%lu MHz",
                 khz / 1000UL, (khz % 1000UL) / 100UL);
    }
    return t;
}

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

/* --------------------------------------------------------------------- */
/* Info page: compiler, build date, current frequency, drive method, the
   IO map and the MCU unique device ID. `invert` swaps fg/bg (white
   background page).                                                     */
static void info_demo(uint32_t ms, uint8_t invert)
{
    char buf[24];
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

    printf("[LCD] info%s: compiler=%s build=%s %s\r\n",
           invert ? " (inverted)" : "", comp, __DATE__, __TIME__);
    printf("[LCD] info: freq=%lu MHz drive=%s\r\n",
           mhz, LCD_BusIsHw() ? "HW SPI1" : "soft bit-bang");
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

    const int ix = 2;
    int y = 2;
    snprintf(buf, sizeof buf, "%s", comp);
    LCD_DisplayString(ix, (uint16_t)y, buf);  y += INFO_DY;

    snprintf(buf, sizeof buf, "Build %s", __DATE__);
    LCD_DisplayString(ix, (uint16_t)y, buf);  y += INFO_DY;

    snprintf(buf, sizeof buf, "Freq %lu MHz", mhz);
    LCD_DisplayString(ix, (uint16_t)y, buf);  y += INFO_DY;

    if (LCD_BusIsHw())
    {
        snprintf(buf, sizeof buf, "HW SPI1 %s",
                 mhz_text(LCD_HwSpiKHz()));
    }
    else
    {
        snprintf(buf, sizeof buf, "Soft %s",
                 mhz_text(LCD_SoftKHz()));
    }
    LCD_DisplayString(ix, (uint16_t)y, buf);  y += INFO_DY;

    snprintf(buf, sizeof buf, "SCL=PA5 SDA=PA7");
    LCD_DisplayString(ix, (uint16_t)y, buf);  y += INFO_DY;

    snprintf(buf, sizeof buf, "CS=PA4 DC=PA6(low)");
    LCD_DisplayString(ix, (uint16_t)y, buf);  y += INFO_DY;

    snprintf(buf, sizeof buf, "TOUCH PA2/PA3");
    LCD_DisplayString(ix, (uint16_t)y, buf);  y += INFO_DY;

    snprintf(buf, sizeof buf, "UID %08lX", (unsigned long)uid[0]);
    LCD_DisplayString(ix, (uint16_t)y, buf);  y += INFO_DY;

    /* Solid-color fill durations, measured earlier in this pass (the
     * TEST_STAND solids run before the info pages) - so they always
     * reflect the current driving method. */
    for (int i = 0; i < 5; i++)
    {
        snprintf(buf, sizeof buf, "%-5s : %4lu ms",
                 g_solid_name[i], (unsigned long)g_solid_ms[i]);
        LCD_DisplayString(ix, (uint16_t)y, buf);  y += INFO_DY;
    }

    g_fps_color = LCD_WHITE;              /* restore default FPS glyph color */

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

    /* 8 px/glyph: center each line dynamically. */
    LCD_DisplayString((uint16_t)((LCD_W() - (int)strlen(l1) * 8) / 2), 40,
                      (char *)l1);
    LCD_DisplayString((uint16_t)((LCD_W() - (int)strlen(l2) * 8) / 2), 64,
                      (char *)l2);

    HAL_Delay(ms);

    LCD_SetAsciiFont(&ASCII_Font12);      /* back to the normal font      */
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

    printf("\r\n==== nano-f411 (STM32F411CEU6) nv3030b_md183_240x284_cst816d @ %lu MHz ====\r\n",
           (unsigned long)(SystemCoreClock / 1000000UL));
    printf("NV3030B 1.83\" 240x284 (wrapped-command SPI, MADCTL 0x08):\r\n");
    printf("SCL=PA5 SDA=PA7 CS=PA4; DC=PA6 driven low (vendor style)\r\n");
    printf("TOUCH: CST816D I2C SCL=PA2 SDA=PA3\r\n");

    Touch_Init();
    LCD_UseSoftBus();     /* start on the vendor-style bit-bang path */
    LCD_Init();
    LCD_SetAsciiFont(&ASCII_Font12);
    paint_fps_band();

    while (1)
    {
        /* ---- SOFT (bit-bang ~2 MHz) pass ---- */
        printf("[LCD] phase: SOFT banner\r\n");
        LCD_Reinit();         /* re-frame the panel */
        banner_page("NV3030B", "SOFT bus test",
                    LCD_BLACK, LCD_YELLOW, 3000);

        printf("[LCD] running patterns on SOFT bit-bang @ %s\r\n",
               mhz_text(LCD_SoftKHz()));
        run_patterns();

        /* ---- HARDWARE (SPI1 @ 50 MHz) pass ---- */
        LCD_UseHwBus();
        printf("[LCD] phase: HARDWARE banner\r\n");
        LCD_Reinit();         /* re-frame the panel */
        banner_page("NV3030B", "HW SPI1 test",
                    LCD_BLACK, LCD_CYAN, 3000);

        printf("[LCD] running patterns on HARDWARE SPI1 @ %s\r\n",
               mhz_text(LCD_HwSpiKHz()));
        run_patterns();

        /* ---- NES-size (224x256) window, HW only ---- */
        LCD_SetWindow(8U, 14U, 224U, 256U);   /* centered NES window */
        banner_page("NES 224x256", "HW SPI1 test",
                    LCD_BLACK, LCD_CYAN, 3000);
        printf("[LCD] running NES-size patterns on HARDWARE SPI1 @ %s\r\n",
               mhz_text(LCD_HwSpiKHz()));
        run_patterns();
        LCD_ResetWindow();

        LCD_UseSoftBus();
    }

    return 0;
}