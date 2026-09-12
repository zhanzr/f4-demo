/*
  ili9163c_md144_128x128 main for the nano-f411 (STM32F411CEU6 @ 100 MHz).
  ILI9163C 1.44" 128x128 module (MD144 connector), same pattern set on
  BOTH drive methods:

    - big-font banner page (8x16 font), then the full pattern set:
      info page (normal + inverted), TEST_STAND screens,
      HSV gradient sweep, LED test - with a live FPS counter throughout.

  The module connector has NO D/C pin and NO backlight pin: the panel is
  strapped for 3-wire serial (every byte is a 9-bit frame - the D/C bit is
  clocked first, then 8 data bits), and the backlight is hardwired
  on-module (always on after power).

  Wiring: SCL=PA5, SDA=PA7, RES=PA6, CS=PB8.

  Clock: SystemClock_Config is overridden (weak hook) to run APB2 at
  100 MHz instead of the board-default 50 MHz, so the HW SPI1 path can
  clock the panel at 12.5 MHz (prescaler /8; the F411 SPI only produces
  8/16-bit frames, so the HW path transmits 16-bit words packed with the
  9-bit frame bitstream). The core stays at 100 MHz; USART1 (APB2)
  recomputes its baud from the live PCLK2, so the console stays at 115200.
*/

#include <stdio.h>
#include "board.h"
#include "lcd.h"
#include "interface.h"
#include "lcd/lcd_font_1608.h"

/* ---- clock override: APB2 = 100 MHz (HCLK/1) for the HW SPI1 path ---- */
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

#define SCREEN_W   LCD_Width     /* 128 */
#define SCREEN_H   LCD_Height    /* 128 */
#define FPS_BAND   20            /* bottom rows reserved for the FPS text   */
#define ANIM_H     (SCREEN_H - FPS_BAND)
#define BACK_COLOR LCD_BLACK
#define LED_HALF   1000          /* LED test dwell (ms)                    */

/* 128 px / 6 px per glyph = 21 chars per line with the 6x12 font. */
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
/* Vendor TEST_STAND screens (128x128).                                  */
static void TEST_STAND(void)
{
    DispFrame();
    StopDelay(Delay_Time);

    DispGrayHor16();
    StopDelay(Delay_Time);

    DispBand();
    StopDelay(Delay_Time);

    DispColor(RED);   StopDelay(Delay_Time);
    DispColor(GREEN); StopDelay(Delay_Time);
    DispColor(BLUE);  StopDelay(Delay_Time);
    DispColor(WHITE); StopDelay(Delay_Time);
    DispColor(BLACK); StopDelay(Delay_Time);
}

/* --------------------------------------------------------------------- */
/* Info page: compiler, build date, current frequency, drive method, the
   IO map and the MCU unique device ID. `invert` swaps fg/bg (white
   background page). 7 compact lines sized for the 128 px width (21
   chars/line with the 6x12 font); no ADC block (the board layer provides
   no ADC helper).                                                       */
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
           mhz, LCD_BusIsHw() ? "HW SPI1 (packed 9-bit)" : "soft bit-bang");
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
        unsigned long khz = LCD_HwSpiKHz();
        snprintf(buf, sizeof buf, "SPI1 %lu.%lu MHz (HW)",
                 khz / 1000UL, (khz % 1000UL) / 100UL);
    }
    else
    {
        snprintf(buf, sizeof buf, "Bus soft (3-wire)");
    }
    LCD_DisplayString(ix, (uint16_t)y, buf);  y += INFO_DY;

    snprintf(buf, sizeof buf, "SCL=PA5 SDA=PA7");
    LCD_DisplayString(ix, (uint16_t)y, buf);  y += INFO_DY;

    snprintf(buf, sizeof buf, "RST=PA6 CS=PB8");
    LCD_DisplayString(ix, (uint16_t)y, buf);  y += INFO_DY;

    snprintf(buf, sizeof buf, "UID %08lX", (unsigned long)uid[0]);
    LCD_DisplayString(ix, (uint16_t)y, buf);

    g_fps_color = LCD_WHITE;              /* restore default FPS glyph color */

    uint32_t start = HAL_GetTick();
    do
    {
        fps_update();
        HAL_Delay(50);
    } while (HAL_GetTick() - start < ms);
}

/* --------------------------------------------------------------------- */
/* Big-font banner page (8x16 font).                                     */
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

    /* 8 px/glyph: center 13-char lines -> x = (128 - 13*8) / 2 = 12. */
    LCD_DisplayString(12, 40, (char *)l1);
    LCD_DisplayString(12, 64, (char *)l2);

    HAL_Delay(ms);

    LCD_SetAsciiFont(&ASCII_Font12);      /* back to the normal font      */
    g_fps_color = LCD_WHITE;
}

/* --------------------------------------------------------------------- */
/* Full test-pattern set.                                                */
static void run_patterns(void)
{
    printf("[LCD] phase: info\r\n");
    info_demo(5000, 0);

    printf("[LCD] phase: info (inverted colors)\r\n");
    info_demo(5000, 1);

    printf("[LCD] phase: vendor TEST_STAND\r\n");
    TEST_STAND();

    printf("[LCD] phase: gradient\r\n");
    gradient_demo(1000);

    printf("[LCD] phase: LED test\r\n");
    led_test();
}

/* --------------------------------------------------------------------- */
int main(void)
{
    HAL_Init();
    Board_Init();

    printf("\r\n==== nano-f411 (STM32F411CEU6) ili9163c_md144_128x128 @ %lu MHz ====\r\n",
           (unsigned long)(SystemCoreClock / 1000000UL));
    printf("ILI9163C 1.44\" 128x128, 3-wire serial (9-bit frames: D/C bit "
           "+ 8 data):\r\n");
    printf("SCL=PA5 SDA=PA7 RES=PA6 CS=PB8; no DC pin (in-protocol), "
           "backlight hardwired on-module\r\n");

    LCD_Init();
    LCD_SetAsciiFont(&ASCII_Font12);
    paint_fps_band();

    while (1)
    {
        /* ---- SOFT (bit-banged) bus ---- */
        printf("[LCD] phase: SOFT banner\r\n");
        LCD_UseSoftBus();
        LCD_Reinit();         /* re-frame the panel for the soft bus */
        banner_page("now will do", "soft SPI test",
                    LCD_YELLOW, LCD_BLUE, 3000);

        printf("[LCD] running patterns on SOFT SPI\r\n");
        run_patterns();

        /* ---- HARDWARE (SPI1, packed 9-bit frames) bus ---- */
        printf("[LCD] phase: HARDWARE banner\r\n");
        LCD_UseHwBus();
        LCD_Reinit();         /* re-frame the panel for the HW bus */
        banner_page("now will do", "HW SPI1 test",
                    LCD_BLACK, LCD_CYAN, 3000);

        printf("[LCD] running patterns on HARDWARE SPI1 @ %lu.%lu MHz\r\n",
               LCD_HwSpiKHz() / 1000UL, (LCD_HwSpiKHz() % 1000UL) / 100UL);
        run_patterns();
    }

    return 0;
}