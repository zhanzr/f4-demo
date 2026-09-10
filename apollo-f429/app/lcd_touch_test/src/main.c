/*
  lcd_touch_test main for the apollo-f429 (STM32F429IGT6 @ 180 MHz).

  Drives the on-board 2.8" TFT LCD (16-bit FSMC parallel bus, ILI9341-family)
  with the vendored ALIENTEK Apollo driver, ported per tft_lcd_test +
  touch/实验30. The demo runs, forever:

    1. Info page + inverted info page (same 4 s duration): model, LCD ID, core
       clock, compiler name/version, ADC Vdda/VREFINT, DHT11 (PB12)
       temp/humidity, all with touch active.
    2. Vendor TFTLCD TEST loop (12 background colors + text banner).
    3. The st7789_md169 test patterns: TEST_STAND (DispFrame / DispGrayHor16 /
       DispBand / DispColor), a short HSV gradient sweep, an LED check, a FPS
       counter.
    4. Touch polled on every page: press draws a big red point, the "RST"
       corner clears the screen.

  Console: USART1 PA9/PA10 via the board layer (printf). Backlight = PB5.
  LED0 (PB1) / LED1 (PB0) are the on-board blinkers.
*/

#include <stdio.h>
#include "board.h"
#include "adc_internal.h"
#include "dht11.h"
#include "pcf8574.h"
#include "lcd.h"
#include "touch.h"

#define STRFY_(x) #x
#define STRFY(x)  STRFY_(x)
#if defined(__ARMCC_VERSION)
#define COMPILER_STR "AC6 armclang " STRFY(__ARMCC_VERSION)
#elif defined(__clang__)
#define COMPILER_STR "Clang " STRFY(__clang_major__) "." STRFY(__clang_minor__) "." STRFY(__clang_patchlevel__)
#elif defined(__GNUC__)
#define COMPILER_STR "GCC " STRFY(__GNUC__) "." STRFY(__GNUC_MINOR__) "." STRFY(__GNUC_PATCHLEVEL__)
#else
#define COMPILER_STR "unknown compiler"
#endif

#define SCREEN_W   LCD_Width     /* 240 */
#define SCREEN_H   LCD_Height    /* 320 */
#define ANIM_H     SCREEN_H      /* full panel; FPS is overlaid at the bottom */
#define INFO_MS    4000          /* info pages dwell */
#define GRAD_MS    1200          /* gradient dwell (shortened) */
#define TOUCH_MS   10000         /* touch drawing phase dwell */
#define LED_HALF   1000

#define VREFINT_TYPICAL_MV 1210U
/* Factory 2-point temp-sensor calibration (12-bit ADC code @ 3.3 V). */
#define TS_CAL1_ADDR  ((uint16_t *)0x1FFF7A2CU)   /* code at 30 C  */
#define TS_CAL2_ADDR  ((uint16_t *)0x1FFF7A2EU)   /* code at 110 C */

/* Vendor TEST loop colors (tft_lcd_test main, in order). */
static const uint16_t vendor_colors[12] = {
    WHITE, BLACK, BLUE, RED, MAGENTA, GREEN,
    CYAN, YELLOW, BRRED, GRAY, LGRAY, BROWN
};

/* --------------------------------------------------------------------- */
static volatile uint32_t g_frames;
static uint32_t         g_last_frames;
static uint32_t         g_fps_last_tick;
static uint32_t         g_fps_color = LCD_WHITE;

static void fps_frame(void)   { g_frames++; }

static void fps_update(void)
{
    uint32_t now = HAL_GetTick();
    if (now - g_fps_last_tick < 1000) { return; }
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
    LCD_ShowTransparent(1);
    LCD_DisplayString(2, SCREEN_H - 14, buf);
    LCD_ShowTransparent(0);
}

/* --------------------------------------------------------------------- */
/* Non-blocking touch: poll + draw a big red point wherever pressed.      */
static void touch_poll(void)
{
    tp_dev.scan(0);
    if (tp_dev.sta & TP_PRES_DOWN)
    {
        if (tp_dev.x[0] < lcddev.width && tp_dev.y[0] < lcddev.height)
        {
            if (tp_dev.x[0] > (lcddev.width - 24) && tp_dev.y[0] < 16)
            {
                LCD_Clear(WHITE);
                POINT_COLOR = BLUE;
                LCD_ShowString(lcddev.width - 24, 0, 200, 16, 16, (uint8_t *)"RST");
            }
            else
            {
                TP_Draw_Big_Point(tp_dev.x[0], tp_dev.y[0], RED);
            }
        }
    }
}

static void delay_with_fps(uint32_t ms)
{
    uint32_t start = HAL_GetTick();
    do
    {
        fps_update();
        touch_poll();
        HAL_Delay(50);
    } while (HAL_GetTick() - start < ms);
}

/* --------------------------------------------------------------------- */
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

static void gradient_demo(uint32_t ms)
{
    static uint16_t row[SCREEN_W];
    uint32_t start = HAL_GetTick();
    uint32_t t = 0;

    do
    {
        int hue_a = (int)(t * 3600 / ms);
        int hue_b = hue_a + 1800;
        if (hue_b >= 3600) { hue_b -= 3600; }
        for (int y = 0; y < ANIM_H; y++)
        {
            int frac = y * 1000 / ANIM_H;
            int hue  = hue_a + (hue_b - hue_a) * frac / 1000;
            uint32_t c = hsv_to_rgb(hue, 255, 255);
            uint16_t rgb565 = (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) |
                                         ((c >> 3) & 0x001F));
            for (int x = 0; x < SCREEN_W; x++) { row[x] = rgb565; }
            LCD_CopyBuffer(0, (uint16_t)y, SCREEN_W, 1, row);
        }
        fps_frame();
        fps_update();
        touch_poll();
        t = HAL_GetTick() - start;
    } while (t < ms);
}

/* Die temperature from ADC1_IN18 using the factory 2-point calibration.
 * adc.raw_temp code is re-scaled to the 3.3 V reference (via measured VDDA). */
static int DieTempC(const ADC_InternalResult *adc, uint32_t vdda_mv)
{
    uint32_t cal1 = *TS_CAL1_ADDR;
    uint32_t cal2 = *TS_CAL2_ADDR;
    uint32_t scaled;

    if (vdda_mv == 0U) { return 0; }
    scaled = ((uint32_t)adc->raw_temp * 3300UL) / vdda_mv;
    if (scaled <= cal1) { return 30; }
    if (scaled >= cal2) { return 110; }
    return 30 + (int)((scaled - cal1) * 80UL / (cal2 - cal1));
}

/* Info page (normal or inverted fg/bg). Adds the live ADC + DHT11 reads. */
static void info_page(uint8_t invert)
{
    ADC_InternalResult adc;
    DHT11_Result dht;
    char buf[36];
    uint32_t fg = invert ? LCD_BLACK : LCD_WHITE;
    uint32_t bg = invert ? LCD_WHITE : LCD_BLACK;

    LCD_SetAsciiFont(&ASCII_Font12);
    LCD_SetColor(fg);
    LCD_SetBackColor(bg);
    LCD_ClearBg();
    LCD_SetColor(fg);
    g_fps_color = fg;

    LCD_DisplayString(20, 0, "apollo-f429 LCD 2.8\" TFT");
    LCD_DisplayString(20, 18, "16-bit FSMC parallel bus");
    if (lcddev.id == 0x9341U)
    {
        snprintf(buf, sizeof buf, "ID = 0x%04X (ILI9341)", (unsigned)lcddev.id);
    }
    else
    {
        snprintf(buf, sizeof buf, "ID = 0x%04X (not 0x9341)", (unsigned)lcddev.id);
    }
    LCD_DisplayString(20, 36, buf);

    snprintf(buf, sizeof buf, "core %lu MHz  %s",
             (unsigned long)SystemCoreClock / 1000000UL, COMPILER_STR);
    LCD_DisplayString(20, 54, buf);

    ADC_Internal_Sample(&adc);
    uint32_t vdda = (1210UL * 4095UL) / adc.raw_vrefint;
    snprintf(buf, sizeof buf, "Vdda %lu mV  VREFINT %u",
             (unsigned long)vdda, adc.raw_vrefint);
    LCD_DisplayString(20, 72, buf);

    int die_c = DieTempC(&adc, vdda);
    snprintf(buf, sizeof buf, "ADC die T %d C", die_c);
    LCD_DisplayString(20, 90, buf);

    /* PB12 is shared: the PCF8574 INT holds it low. Release the expander
     * interrupt first, otherwise the DHT11 line reads stuck-low. */
    PCF8574_ReleaseINT();
    if (DHT11_Read(&dht) && dht.valid)
    {
        printf("[DHT11] T %d.%d C  RH %d.%d %%\r\n",
               dht.t_int, dht.t_dec, dht.rh_int, dht.rh_dec);
        snprintf(buf, sizeof buf, "DHT11 T %d.%d C  RH %d.%d %%",
                 dht.t_int, dht.t_dec, dht.rh_int, dht.rh_dec);
        LCD_DisplayString(20, 108, buf);
    }
    else
    {
        printf("[DHT11] read failed (stage %d)\r\n", dht.fail_stage);
        LCD_DisplayString(20, 108, "DHT11 no sensor / bad chk");
    }

    LCD_DisplayString(20, 126, "touch active - press to draw,");
    LCD_DisplayString(20, 144, "RST top-right (24,0) clears");
}

static void led_test(void)
{
    printf("[LCD] LED ON\r\n");
    LED0_ON();  LED1_OFF();
    delay_with_fps(LED_HALF);
    printf("[LCD] LED OFF\r\n");
    LED0_OFF(); LED1_ON();
    delay_with_fps(LED_HALF);
}

static void TEST_STAND(void)
{
    DispFrame();            StopDelay(500);
    DispGrayHor16();        StopDelay(500);
    DispBand();             StopDelay(500);
    DispColor(RED);   StopDelay(500);
    DispColor(GREEN); StopDelay(500);
    DispColor(BLUE);  StopDelay(500);
    DispColor(WHITE); StopDelay(500);
    DispColor(BLACK); StopDelay(500);
}

/* --------------------------------------------------------------------- */
int main(void)
{
    u8 x = 0;

    HAL_Init();
    Board_Init();

    printf("\r\n==== apollo-f429 stage-2 app: lcd_touch_test @ %lu MHz ====\r\n",
           (unsigned long)SystemCoreClock / 1000000UL);
    ADC_Internal_Init();
    PCF8574_Init();              /* release shared PB12 (PCF8574 INT / DHT11) */
    DHT11_Init();

    LCD_Init();
    printf("done: LCD_Init id=0x%04X\r\n", (unsigned)lcddev.id);
    LCD_SetColor(LCD_WHITE);
    LCD_SetBackColor(LCD_BLACK);
    LCD_ClearBg();

    /* Touch auto-detect + (first boot) 4-point calibration on the AT24C02. */
    printf("touch: tp_dev.init...\r\n");
    tp_dev.init();
    printf("done: touchtype=0x%02X\r\n", (unsigned)tp_dev.touchtype);

    while (1)
    {
        /* 1. info page first, then inverted (same duration). */
        printf("[LCD] phase: info\r\n");
        info_page(0);
        delay_with_fps(INFO_MS);

        printf("[LCD] phase: info (inverted)\r\n");
        info_page(1);
        delay_with_fps(INFO_MS);

        /* 2. vendor TFTLCD TEST loop: 12 background colors + banner. */
        printf("[LCD] phase: vendor color cycle\r\n");
        for (x = 0; x < 12; x++)
        {
            LCD_Clear(vendor_colors[x]);
            POINT_COLOR = RED;
            LCD_ShowString(10, 40, 240, 16, 16, (uint8_t *)"Apollo STM32F4/F7");
            LCD_ShowString(10, 60, 240, 16, 16, (uint8_t *)"TFTLCD TEST");
            LCD_ShowString(10, 80, 240, 16, 16, (uint8_t *)"ATOM@ALIENTEK");
            char idbuf[12];
            snprintf(idbuf, sizeof idbuf, "LCD ID:%04X", (unsigned)lcddev.id);
            LCD_ShowString(10, 100, 240, 16, 16, (uint8_t *)idbuf);
            LED0_TOGGLE();
            HAL_Delay(800);

            touch_poll();
        }

        /* 3. st7789-style patterns. */
        printf("[LCD] phase: TEST_STAND\r\n");
        TEST_STAND();

        printf("[LCD] phase: gradient\r\n");
        gradient_demo(GRAD_MS);

        printf("[LCD] phase: LED test\r\n");
        led_test();

        printf("[LCD] phase: touch (draw on white, RT clears)\r\n");
        LCD_Clear(WHITE);
        POINT_COLOR = BLUE;
        LCD_ShowString(lcddev.width - 24, 0, 200, 16, 16, (uint8_t *)"RST");
        POINT_COLOR = RED;
        delay_with_fps(TOUCH_MS);
    }

    return 0;
}