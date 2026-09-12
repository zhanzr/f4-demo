/*
  st7365_md350_320x480 main for the nano-f411 (STM32F411CEU6 @ 100 MHz).
  MD350 module: ST7365P 3.5" 320x480 (windows at COL_Pre = ROW_Pre = 0,
  MADCTL 0x48 portrait, 16bpp).

  Pattern set, on BOTH drive methods (soft bit-bang / HW SPI1 @ 50 MHz):
    - big-font banner page (8x16 font), then the full pattern set:
      info page (normal + inverted), TEST_STAND screens,
      HSV gradient sweep, LED test - with a live FPS counter throughout.

  Wiring: SCL=PA5, SDA=PA7, RES=PA3, DC=PA4, CS=PB8, BL=PB9 (TIM4_CH4
  PWM, 15%). MISO=PA6 exists on the module but is not used by this
  TX-only driver (it maps to SPI1_MISO at AF5 for full-duplex read-back).

  Clock: SystemClock_Config is overridden (weak hook) to run APB2 at
  100 MHz instead of the board-default 50 MHz, so SPI1 clocks the panel
  at 50 MHz (prescaler /2 - the F411 SPI1 max). The core stays at
  100 MHz; USART1 (APB2) recomputes its baud from the live PCLK2, so the
  console stays at 115200.
*/

#include <stdio.h>
#include <string.h>
#include "board.h"
#include "lcd.h"
#include "interface.h"
#include "lcd/lcd_font_1608.h"
#include "backlight.h"

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

#define SCREEN_W   LCD_Width     /* 320 */
#define SCREEN_H   LCD_Height    /* 480 */
#define FPS_BAND   20            /* bottom rows reserved for the FPS text   */
#define ANIM_H     (SCREEN_H - FPS_BAND)
#define BACK_COLOR LCD_BLACK
#define LED_HALF   1000          /* LED test dwell (ms)                    */

/* The 320 px panel fits 53 chars/line with the 6x12 font; the info page
 * keeps compact 21-char lines anyway. */
#define INFO_DY    13

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
        LCD_DisplayString(1, ANIM_H + 4, buf);
        LCD_ShowTransparent(0);
    }
}

static void paint_fps_band(void)
{
    LCD_SetColor(BACK_COLOR);
    LCD_SetBackColor(BACK_COLOR);
    LCD_FillRect(0, ANIM_H, SCREEN_W, FPS_BAND);
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
    for (int y = 0; y < ANIM_H; y++)
    {
        int frac = y * 1000 / ANIM_H;
        int hue  = hue_a + (hue_b - hue_a) * frac / 1000;
        uint32_t c = hsv_to_rgb(hue, 255, 255);
        uint16_t rgb565 = (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) |
                                     ((c >> 3) & 0x001F));
        for (int x = 0; x < SCREEN_W; x++)
        {
            row[x] = rgb565;
        }
        LCD_CopyBuffer(0, (uint16_t)y, SCREEN_W, 1, row);
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
    LCD_FillRect(0, ANIM_H, SCREEN_W, FPS_BAND);
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
        snprintf(buf, sizeof buf, "SPI1 %lu MHz (HW)",
                 LCD_HwSpiKHz() / 1000UL);
    }
    else
    {
        snprintf(buf, sizeof buf, "Bus soft (bit-bang)");
    }
    LCD_DisplayString(ix, (uint16_t)y, buf);  y += INFO_DY;

    /* Panel IC ID, read over MISO (0xD3 read-ID4: 1 dummy + 3 ID bytes;
     * the 0x04 RDDID command is not supported by this controller). */
    {
        uint8_t id[3] = { 0, 0, 0 };
        LCD_ReadBytes(0xD3, id, 1U, 3U);
        printf("[LCD] info: IC ID (0xD3): %02X %02X %02X\r\n",
               id[0], id[1], id[2]);
        if (id[0] == 0xFFU && id[1] == 0xFFU && id[2] == 0xFFU)
        {
            snprintf(buf, sizeof buf, "ID --");
        }
        else
        {
            snprintf(buf, sizeof buf, "ID %02X %02X %02X",
                     id[0], id[1], id[2]);
        }
        LCD_DisplayString(ix, (uint16_t)y, buf);  y += INFO_DY;
    }

    snprintf(buf, sizeof buf, "SCL=PA5 SDA=PA7");
    LCD_DisplayString(ix, (uint16_t)y, buf);  y += INFO_DY;

    snprintf(buf, sizeof buf, "RST=PA3 DC=PA4");
    LCD_DisplayString(ix, (uint16_t)y, buf);  y += INFO_DY;

    snprintf(buf, sizeof buf, "CS=PB8 BL=%u%%", (unsigned)duty);
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
    LCD_FillRect(0, ANIM_H, SCREEN_W, FPS_BAND);
    LCD_SetColor(fg);
    LCD_SetBackColor(bg);

    /* 8 px/glyph: center each line dynamically. */
    LCD_DisplayString((uint16_t)((SCREEN_W - (int)strlen(l1) * 8) / 2), 40,
                      (char *)l1);
    LCD_DisplayString((uint16_t)((SCREEN_W - (int)strlen(l2) * 8) / 2), 64,
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

    printf("\r\n==== nano-f411 (STM32F411CEU6) st7365_md350_320x480 @ %lu MHz ====\r\n",
           (unsigned long)(SystemCoreClock / 1000000UL));
    printf("ST7365P 3.5\" 320x480 (MD350, MADCTL 0x48 portrait), "
           "4-wire SPI (soft bit-bang + HW SPI1 @ 50 MHz):\r\n");
    printf("SCL=PA5 SDA=PA7 RES=PA3 DC=PA4 CS=PB8 BL=PB9(PWM 15%%) "
           "MISO=PA6(unused)\r\n");

    Backlight_Init();
    Backlight_SetDuty(15U);
    LCD_Init();
    LCD_SetAsciiFont(&ASCII_Font12);
    paint_fps_band();

    while (1)
    {
        /* ---- SOFT (bit-banged) bus ---- */
        printf("[LCD] phase: SOFT banner\r\n");
        LCD_UseSoftBus();
        Backlight_SetDuty(15U);   /* dimmer during the slow bit-bang pass */
        LCD_Reinit();         /* re-frame the panel for the soft bus */
        banner_page("MD350 320x480", "soft SPI test",
                    LCD_YELLOW, LCD_BLUE, 3000);

        printf("[LCD] running patterns on SOFT SPI\r\n");
        run_patterns();

        /* ---- HARDWARE (SPI1 @ 50 MHz) bus ---- */
        printf("[LCD] phase: HARDWARE banner\r\n");
        LCD_UseHwBus();
        Backlight_SetDuty(15U);
        LCD_Reinit();         /* re-frame the panel for the HW bus */
        banner_page("MD350 320x480", "HW SPI1 test",
                    LCD_BLACK, LCD_CYAN, 3000);

        printf("[LCD] running patterns on HARDWARE SPI1 @ %lu MHz\r\n",
               LCD_HwSpiKHz() / 1000UL);
        run_patterns();
    }

    return 0;
}