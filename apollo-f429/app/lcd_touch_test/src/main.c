/*
  lcd_touch_test - apollo-f429 stage-2 app (SDRAM/NAND boot) + LCD + touch.

  The SDRAM-linked twin of bare/lcd_touch_test: booted by the app/tool/boot
  stage-1 bootloader into SDRAM at 0xC0000000, driving the same 2.8" TFT LCD
  (16-bit FSMC parallel bus, ILI9341-family) with the vendored ALIENTEK Apollo
  driver (tft_lcd_test + touch/实验30). The driver sources are shared with
  bare/lcd_touch_test; only this main.c + system_app.c differ. Three things:

    1. LCD init + ID read, then the vendor's TFTLCD TEST loop (12 background
       colors cycling every second with a text banner).
    2. The st7789_md169 reference test patterns: info page, TEST_STAND
       (DispFrame / DispGrayHor16 / DispBand / DispColor), HSV gradient sweep,
       LED check, live FPS - with touch active on every page.
    3. Touch: tp_dev.init() (auto-detect + first-boot 4-point calibration on
       the AT24C02), then a scan draws a big point wherever pressed; the "RST"
       corner clears.

  Console: USART1 PA9/PA10 via the board layer (printf). Backlight = PB5.
*/

#include <stdio.h>
#include "board.h"
#include "adc_internal.h"
#include "lcd.h"
#include "touch.h"

#define SCREEN_W   LCD_Width     /* 240 */
#define SCREEN_H   LCD_Height    /* 320 */
#define ANIM_H     SCREEN_H      /* full panel; FPS is overlaid at the bottom */
#define BACK_COLOR LCD_BLACK
#define LED_HALF   1000

/* Vendor TEST loop colors (tft_lcd_test main, in order). */
static const uint16_t vendor_colors[12] = {
    WHITE, BLACK, BLUE, RED, MAGENTA, GREEN,
    CYAN, YELLOW, BRRED, GRAY, LGRAY, BROWN
};

/* --------------------------------------------------------------------- */
static volatile uint32_t g_frames;
static uint32_t         g_last_frames;
static uint32_t         g_fps_last_tick;

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
    LCD_SetColor(LCD_WHITE);
    LCD_ShowTransparent(1);
    LCD_DisplayString(2, SCREEN_H - 14, buf);
    LCD_ShowTransparent(0);
}

static void paint_fps_band(void) { /* no reserved band - full panel */ }

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

    paint_fps_band();
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

/* Info page: a few lines of live info (vendor-mirroring text + ADC). */
static void info_page(void)
{
    ADC_InternalResult adc;
    char buf[32];

    LCD_SetAsciiFont(&ASCII_Font12);
    LCD_SetColor(LCD_WHITE);
    LCD_SetBackColor(LCD_BLACK);
    LCD_ClearBg();
    paint_fps_band();
    LCD_SetColor(LCD_WHITE);

    LCD_DisplayString(20, 0, "apollo-f429 LCD 2.8\" ILI9341");
    LCD_DisplayString(20, 18, "16-bit FSMC parallel bus");
    LCD_DisplayString(20, 36, "ID = 0x9341 (ILI9341)");

    snprintf(buf, sizeof buf, "core %lu MHz", (unsigned long)SystemCoreClock / 1000000UL);
    LCD_DisplayString(20, 54, buf);

    ADC_Internal_Sample(&adc);
    uint32_t vdda = (1210UL * 4095UL) / adc.raw_vrefint;
    snprintf(buf, sizeof buf, "Vdda %lu mV  VREFINT %u", (unsigned long)vdda, adc.raw_vrefint);
    LCD_DisplayString(20, 72, buf);

LCD_DisplayString(20, 90, "touch active - press to draw,");
        LCD_DisplayString(20, 108, " RT corner clears; all pages");
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
        /* 1. vendor TFTLCD TEST loop: 12 background colors + banner. */
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
            LCD_ShowString(10, 120, 240, 12, 12, (uint8_t *)"2024/1/6");
            LED0_TOGGLE();
            HAL_Delay(800);

            tp_dev.scan(0);
            if ((tp_dev.sta & TP_PRES_DOWN) && tp_dev.x[0] < lcddev.width &&
                tp_dev.y[0] < lcddev.height)
            {
                TP_Draw_Big_Point(tp_dev.x[0], tp_dev.y[0], RED);
            }
        }

        /* 2. st7789-style patterns. */
        printf("[LCD] phase: info\r\n");
        LCD_SetColor(LCD_WHITE); LCD_SetBackColor(LCD_BLACK);
        LCD_ClearBg();
        info_page();
        delay_with_fps(4000);

        printf("[LCD] phase: TEST_STAND\r\n");
        TEST_STAND();

        printf("[LCD] phase: gradient\r\n");
        gradient_demo(3000);

        printf("[LCD] phase: LED test\r\n");
        led_test();

        printf("[LCD] phase: touch (draw on white, RT clears)\r\n");
        LCD_Clear(WHITE);
        POINT_COLOR = BLUE;
        LCD_ShowString(lcddev.width - 24, 0, 200, 16, 16, (uint8_t *)"RST");
        POINT_COLOR = RED;
        delay_with_fps(10000);
    }

    return 0;
}