#include <stdio.h>
#include <string.h>
#include "board.h"
#include "st7735s.h"

static uint16_t fb[LCD_WIDTH * LCD_HEIGHT];

static uint16_t px(int x, int y) { return fb[(uint16_t)y * LCD_WIDTH + (uint16_t)x]; }
static void set(int x, int y, uint16_t c)
{
    if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT) return;
    fb[(uint16_t)y * LCD_WIDTH + (uint16_t)x] = c;
}

static void clear(uint16_t c) { for (int i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++) fb[i] = c; }

static void show(const char *name)
{
    printf("%s\r\n", name);
    ST7735_BlitFB(fb);
    HAL_Delay(1200);
}

static void pattern_circle(void)
{
    const int cx = 64, cy = 64, r = 45;
    clear(ST7735_BLACK);
    for (int y = 0; y < LCD_HEIGHT; y++)
    {
        for (int x = 0; x < LCD_WIDTH; x++)
        {
            int dx = x - cx, dy = y - cy;
            int d2 = dx * dx + dy * dy;
            if (d2 >= (r - 2) * (r - 2) && d2 <= r * r) set(x, y, ST7735_CYAN);
        }
    }
}

static void pattern_square(void)
{
    const int x0 = 14, y0 = 14, x1 = 113, y1 = 113;
    clear(ST7735_BLACK);
    for (int x = x0; x <= x1; x++) { set(x, y0, ST7735_MAGENTA); set(x, y1, ST7735_MAGENTA); }
    for (int y = y0; y <= y1; y++) { set(x0, y, ST7735_MAGENTA); set(x1, y, ST7735_MAGENTA); }
}

static void pattern_triangle(void)
{
    const int ax = 64, ay = 16, bx = 12, by = 108, cx2 = 116, cy2 = 108;
    clear(ST7735_BLACK);
    /* very simple triangle rasterisation: for each row, determine span. */
    for (int y = 0; y < LCD_HEIGHT; y++)
    {
        /* param t from apex to base line along each side */
        for (int x = 0; x < LCD_WIDTH; x++)
        {
            /* barycentric-ish check via three half-plane tests. */
            int s1 = (bx - ax) * (y - ay) - (by - ay) * (x - ax);
            int s2 = (cx2 - bx) * (y - by) - (cy2 - by) * (x - bx);
            int s3 = (ax - cx2) * (y - cy2) - (ay - cy2) * (x - cx2);
            if (s1 >= 0 && s2 >= 0 && s3 >= 0) set(x, y, ST7735_YELLOW);
        }
    }
}

static void pattern_color_bars(void)
{
    clear(ST7735_BLACK);
    uint16_t cols[8] = { ST7735_WHITE, ST7735_YELLOW, ST7735_CYAN, ST7735_GREEN,
                         ST7735_MAGENTA, ST7735_RED, ST7735_BLUE, ST7735_BLACK };
    for (int b = 0; b < 8; b++)
    {
        for (int x = b * 16; x < (b + 1) * 16; x++)
            for (int y = 0; y < LCD_HEIGHT; y++) set(x, y, cols[b]);
    }
}

int main(void)
{
    HAL_Init();
    Board_Init();

    printf("\r\n==== nano-f407 ST7735S 1.44\" 128x128 TFT @ 24 MHz ====\r\n");
    printf("SPI2: SCL=PB13 MOSI=PB15 CS=PB12 DC=PC6 RST=PB1 BL=PE9(8%% PWM)\r\n");

    ST7735_Backlight_Init();
    ST7735_Init();

    while (1)
    {
        LED_ON();
        clear(ST7735_RED);      show("fill red");
        clear(ST7735_GREEN);    show("fill green");
        clear(ST7735_BLUE);     show("fill blue");
        LED_OFF();

        pattern_color_bars();   show("color bars");
        pattern_circle();       show("circle");
        pattern_square();       show("square");
        pattern_triangle();     show("triangle");
    }

    return 0;
}
