/*
  st7735_test main for the nano-f411 (STM32F411CEU6 @ 100 MHz).
  Two demo sets, looped forever:
    1. vendor C8T6_md144_t1 TEST_STAND screens (frame / gray / band / solids),
    2. the h723-mini st7789 example's animated patterns, ported to 128x128:
       floating bouncing shapes, pure colors, an animated HSV gradient sweep,
       an LED test, all with a live FPS counter in the bottom band.
  Backlight (PB9, TIM4_CH4) runs at ~20% PWM.

  Wiring: SCL PA5, SDA PA6, RES PA7, DC PA4, CS PB8, BL PB9.
*/

#include <stdio.h>
#include "board.h"
#include "lcd.h"
#include "backlight.h"

#define SCREEN_W   LCD_Width     /* 128 */
#define SCREEN_H   LCD_Height    /* 128 */
#define FPS_BAND   20            /* bottom rows reserved for the FPS text   */
#define ANIM_H     (SCREEN_H - FPS_BAND)
#define BACK_COLOR LCD_BLACK
#define LED_HALF   1000          /* LED test dwell (ms)                    */

/* ------------------------------------------------------------------------ */
/* FPS counter.                                                             */
static volatile uint32_t g_frames;
static uint32_t         g_last_frames;
static uint32_t         g_fps_last_tick;

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
        LCD_SetColor(LCD_WHITE);
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

/* ------------------------------------------------------------------------ */
/* Floating & bouncing shapes.                                              */
typedef enum { SHAPE_SQUARE, SHAPE_CIRCLE, SHAPE_TRIANGLE } shape_kind_t;

typedef struct
{
    shape_kind_t kind;
    int          x, y;
    int          vx, vy;
    int          size;
    uint32_t     color;
} shape_t;

static const uint32_t shape_palette[] = {
    LCD_RED, LCD_GREEN, LCD_BLUE, LCD_YELLOW,
    LCD_CYAN, LCD_MAGENTA, LCD_WHITE,
};

static void init_shapes(shape_t *s, int n)
{
    static const shape_kind_t kinds[3] = { SHAPE_SQUARE, SHAPE_CIRCLE, SHAPE_TRIANGLE };
    for (int i = 0; i < n; i++)
    {
        s[i].kind  = kinds[i % 3];
        s[i].x     = 15 + (i * 31) % (SCREEN_W - 30);
        s[i].y     = 15 + (i * 47) % (ANIM_H - 50);
        s[i].vx    = (i % 2 ? 1 : -1) * (1 + (i % 3));
        s[i].vy    = (i % 3 ? 1 : -1) * (1 + (i % 4));
        s[i].size  = 8 + (i % 4) * 3;
        s[i].color = shape_palette[i % (sizeof(shape_palette) / sizeof(shape_palette[0]))];
    }
}

static void draw_shape(const shape_t *s)
{
    LCD_SetColor(s->color);
    switch (s->kind)
    {
    case SHAPE_SQUARE:
        LCD_FillRect((uint16_t)(s->x - s->size), (uint16_t)(s->y - s->size),
                     (uint16_t)(2 * s->size), (uint16_t)(2 * s->size));
        break;
    case SHAPE_CIRCLE:
        LCD_FillCircle((uint16_t)s->x, (uint16_t)s->y, (uint16_t)s->size);
        break;
    default:
    {
        int r = s->size;
        int x0 = s->x,     y0 = s->y - r;
        int x1 = s->x - r, y1 = s->y + (r * 8) / 10;
        int x2 = s->x + r, y2 = s->y + (r * 8) / 10;
        LCD_DrawLine((uint16_t)x0, (uint16_t)y0, (uint16_t)x1, (uint16_t)y1);
        LCD_DrawLine((uint16_t)x1, (uint16_t)y1, (uint16_t)x2, (uint16_t)y2);
        LCD_DrawLine((uint16_t)x2, (uint16_t)y2, (uint16_t)x0, (uint16_t)y0);
        break;
    }
    }
}

static void step_shape(shape_t *s)
{
    s->x += s->vx;
    s->y += s->vy;
    int r = s->size;
    if (s->x - r < 0)             { s->x = r;                s->vx = -s->vx; }
    if (s->x + r > SCREEN_W - 1)  { s->x = SCREEN_W - 1 - r; s->vx = -s->vx; }
    if (s->y - r < 0)             { s->y = r;                s->vy = -s->vy; }
    if (s->y + r > ANIM_H - 1)    { s->y = ANIM_H - 1 - r;   s->vy = -s->vy; }
}

static void shapes_demo(uint32_t ms)
{
    enum { N = 8 };
    shape_t shapes[N];
    init_shapes(shapes, N);

    LCD_SetBackColor(BACK_COLOR);
    paint_fps_band();

    uint32_t start = HAL_GetTick();
    do
    {
        LCD_ClearRect(0, 0, SCREEN_W, ANIM_H);
        for (int i = 0; i < N; i++)
        {
            step_shape(&shapes[i]);
            draw_shape(&shapes[i]);
        }
        fps_frame();
        fps_update();
    } while (HAL_GetTick() - start < ms);
}

/* ------------------------------------------------------------------------ */
/* Pure colors, one after the other.                                        */
static void colors_demo(uint32_t ms_per_color)
{
    static const uint32_t colors[] = {
        LCD_RED, LCD_GREEN, LCD_BLUE, LCD_YELLOW,
        LCD_CYAN, LCD_MAGENTA, LCD_WHITE, LCD_BLACK,
    };

    for (unsigned i = 0; i < sizeof(colors) / sizeof(colors[0]); i++)
    {
        LCD_SetColor(colors[i]);
        LCD_SetBackColor(colors[i]);
        LCD_Clear();
        paint_fps_band();
        printf("[LCD] pure color %u\r\n", (unsigned)i + 1);
        delay_with_fps(ms_per_color);
    }
}

/* ------------------------------------------------------------------------ */
/* Animated gradient: hue sweeps the full color wheel over `ms`.            */
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

/* ------------------------------------------------------------------------ */
/* LED test (PC13, low active).                                             */
static void led_test(void)
{
    printf("[LCD] LED ON\r\n");
    LED_ON();
    delay_with_fps(LED_HALF);
    printf("[LCD] LED OFF\r\n");
    LED_OFF();
    delay_with_fps(LED_HALF);
}

/* ------------------------------------------------------------------------ */
/* Vendor TEST_STAND screens (128x128).                                     */
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

/* ------------------------------------------------------------------------ */
int main(void)
{
    HAL_Init();
    Board_Init();

    printf("\r\n==== nano-f411 (STM32F411CEU6) st7735_test @ %lu MHz ====\r\n",
           (unsigned long)(SystemCoreClock / 1000000UL));
    printf("ST7735S 1.44\" 128x128, 4-wire SPI: SCL=PA5 SDA=PA6 RES=PA7 "
           "DC=PA4 CS=PB8 BL=PB9(PWM ~20%%)\r\n");

    Backlight_Init();
    LCD_Init();
    LCD_SetAsciiFont(&ASCII_Font12);
    paint_fps_band();

    while (1)
    {
        printf("[LCD] phase: vendor TEST_STAND\r\n");
        TEST_STAND();

        printf("[LCD] phase: shapes\r\n");
        shapes_demo(5000);

        printf("[LCD] phase: pure colors\r\n");
        colors_demo(2000);

        printf("[LCD] phase: gradient\r\n");
        gradient_demo(5000);

        printf("[LCD] phase: LED test\r\n");
        led_test();
    }

    return 0;
}