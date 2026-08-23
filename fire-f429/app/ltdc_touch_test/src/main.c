#include <stdio.h>
#include "board.h"
#include "board_ltdc.h"

/* Screen layout: bottom FPS_BAND rows are a status band (FPS + touch),
 * animations are clipped to ANIM_H. */
#define SCREEN_W    LCD_WIDTH
#define SCREEN_H    LCD_HEIGHT
#define FPS_BAND    32
#define ANIM_H      (SCREEN_H - FPS_BAND)
#define BACK_COLOR  0x00000000U

#define COL_RED     0x00FF0000U
#define COL_GREEN   0x0000FF00U
#define COL_BLUE    0x000000FFU
#define COL_YELLOW  0x00FFFF00U
#define COL_CYAN    0x0000FFFFU
#define COL_MAGENTA 0x00FF00FFU
#define COL_WHITE   0x00FFFFFFU
#define COL_BLACK   0x00000000U
#define COL_ORANGE  0x00FFA500U
#define COL_GRAY    0x00808080U

#define THEME_ACCENT COL_GREEN

/* FPS: g_frames is incremented once per rendered animation frame;
 * status_update() redraws the on-screen FPS + touch line each second. */
static volatile uint32_t g_frames;
static uint32_t          g_last_frames;
static uint32_t          g_fps_last_tick;

static TouchPoint g_touch;
static uint16_t    g_last_x = 0xFFFF;
static uint16_t    g_last_y = 0xFFFF;

static void paint_band(void)
{
    LTDC_FillRect(0, ANIM_H, SCREEN_W, FPS_BAND, BACK_COLOR);
}

static void band_text(const char *s)
{
    LTDC_DrawString(4, ANIM_H + 9, s, COL_WHITE, BACK_COLOR);
}

static void status_update(void)
{
    uint32_t now = HAL_GetTick();
    if (now - g_fps_last_tick >= 1000)
    {
        uint32_t fps = g_frames - g_last_frames;
        g_last_frames = g_frames;
        g_fps_last_tick = now;

        char buf[48];
        if (g_touch.pressed)
        {
            snprintf(buf, sizeof(buf), "FPS:%03lu | T:%u,%u",
                     (unsigned long)fps,
                     (unsigned)g_touch.x, (unsigned)g_touch.y);
        }
        else
        {
            snprintf(buf, sizeof(buf), "FPS:%03lu | T:--",
                     (unsigned long)fps);
        }
        band_text(buf);
    }
}

static void touch_poll(void)
{
    uint16_t clamp_lo = 14;
    uint16_t clamp_hi_x = SCREEN_W - 1 - 13;
    uint16_t clamp_hi_y = SCREEN_H - 1 - 13;

    g_touch = Touch_Scan();

    if (g_touch.pressed && (g_touch.x != 0U || g_touch.y != 0U))
    {
        uint16_t x = g_touch.x;
        uint16_t y = g_touch.y;
        if (x < clamp_lo) x = clamp_lo;
        if (y < clamp_lo) y = clamp_lo;
        if (x > clamp_hi_x) x = clamp_hi_x;
        if (y > clamp_hi_y) y = clamp_hi_y;

        if (x != g_last_x || y != g_last_y)
        {
            printf("Touch: X=%u Y=%u\r\n", (unsigned)x, (unsigned)y);
            g_last_x = x;
            g_last_y = y;
        }

        /* white ring + colored dot so the marker is visible on any background */
        LTDC_FillCircle(x, y, 13, COL_WHITE);
        LTDC_FillCircle(x, y, 8, THEME_ACCENT);
    }
    else if (g_last_x != 0xFFFF)
    {
        printf("Touch: released\r\n");
        g_last_x = 0xFFFF;
        g_last_y = 0xFFFF;
    }
}

static void delay_with_overlay(uint32_t ms)
{
    uint32_t start = HAL_GetTick();
    do
    {
        touch_poll();
        status_update();
        HAL_Delay(20);
    } while (HAL_GetTick() - start < ms);
}

/* ------------------------------------------------------------------ */
/* Bouncing shapes: square / circle / triangle.                       */

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
    COL_RED, COL_GREEN, COL_BLUE, COL_YELLOW,
    COL_CYAN, COL_MAGENTA, COL_WHITE, COL_ORANGE,
};

static void init_shapes(shape_t *s, int n)
{
    static const shape_kind_t kinds[3] = { SHAPE_SQUARE, SHAPE_CIRCLE, SHAPE_TRIANGLE };
    for (int i = 0; i < n; i++)
    {
        s[i].kind  = kinds[i % 3];
        s[i].x     = 40 + (i * 103) % (SCREEN_W - 80);
        s[i].y     = 40 + (i * 197) % (ANIM_H - 120);
        s[i].vx    = (i % 2 ? 1 : -1) * (3 + (i % 4));
        s[i].vy    = (i % 3 ? 1 : -1) * (3 + (i % 5));
        s[i].size  = 20 + (i % 4) * 8;
        s[i].color = shape_palette[i % (sizeof(shape_palette) / sizeof(shape_palette[0]))];
    }
}

static void draw_shape(const shape_t *s)
{
    switch (s->kind)
    {
    case SHAPE_SQUARE:
        LTDC_FillRect((uint16_t)(s->x - s->size), (uint16_t)(s->y - s->size),
                      (uint16_t)(2 * s->size), (uint16_t)(2 * s->size),
                      s->color);
        break;
    case SHAPE_CIRCLE:
        LTDC_FillCircle(s->x, s->y, s->size, s->color);
        break;
    default:
    {
        int r = s->size;
        int x0 = s->x,     y0 = s->y - r;             /* top            */
        int x1 = s->x - r, y1 = s->y + (r * 8) / 10;  /* bottom left    */
        int x2 = s->x + r, y2 = s->y + (r * 8) / 10;  /* bottom right   */
        LTDC_DrawLine(x0, y0, x1, y1, s->color);
        LTDC_DrawLine(x1, y1, x2, y2, s->color);
        LTDC_DrawLine(x2, y2, x0, y0, s->color);
        break;
    }
    }
}

static void step_shape(shape_t *s)
{
    s->x += s->vx;
    s->y += s->vy;
    int r = s->size;
    if (s->x - r < 0)            { s->x = r;               s->vx = -s->vx; }
    if (s->x + r > SCREEN_W - 1) { s->x = SCREEN_W - 1 - r; s->vx = -s->vx; }
    if (s->y - r < 0)            { s->y = r;               s->vy = -s->vy; }
    if (s->y + r > ANIM_H - 1)   { s->y = ANIM_H - 1 - r;  s->vy = -s->vy; }
}

static void shapes_demo(uint32_t ms)
{
    enum { N = 10 };
    shape_t shapes[N];
    init_shapes(shapes, N);

    paint_band();

    uint32_t start = HAL_GetTick();
    do
    {
        LTDC_FillRect(0, 0, SCREEN_W, ANIM_H, BACK_COLOR);
        for (int i = 0; i < N; i++)
        {
            step_shape(&shapes[i]);
            draw_shape(&shapes[i]);
        }
        g_frames++;
        touch_poll();
        status_update();
    } while (HAL_GetTick() - start < ms);
}

/* ------------------------------------------------------------------ */
/* Pure colors, one after the other.                                  */
static void colors_demo(uint32_t ms_per_color)
{
    static const uint32_t colors[] = {
        COL_RED, COL_GREEN, COL_BLUE, COL_YELLOW,
        COL_CYAN, COL_MAGENTA, COL_WHITE, COL_BLACK,
    };

    for (unsigned i = 0; i < sizeof(colors) / sizeof(colors[0]); i++)
    {
        LTDC_Clear(colors[i]);
        paint_band();
        printf("[LCD] pure color %u\r\n", (unsigned)i + 1);
        delay_with_overlay(ms_per_color);
    }
}

/* ------------------------------------------------------------------ */
/* Animated gradient: hue sweeps the full color wheel.                */
static uint32_t hsv_to_rgb888(int h, int s, int v)
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
    paint_band();

    uint32_t start = HAL_GetTick();
    uint32_t t = 0;
    do
    {
        int hue_a = (int)(t * 3600 / ms);
        int hue_b = hue_a + 1800;
        if (hue_b >= 3600) hue_b -= 3600;

        for (int y = 0; y < ANIM_H; y++)
        {
            int frac = y * 1000 / ANIM_H;
            int hue  = hue_a + (hue_b - hue_a) * frac / 1000;
            LTDC_FillRect(0, (uint16_t)y, SCREEN_W, 1, hsv_to_rgb888(hue, 255, 255));
        }

        g_frames++;
        touch_poll();
        status_update();
        HAL_Delay(16);
        t = HAL_GetTick() - start;
    } while (t < ms);
}

/* ------------------------------------------------------------------ */

int main(void)
{
    uint8_t touch_version[4] = {0};

    HAL_Init();
    Board_Init();
    LTDC_Display_Init();
    Touch_Init();

    printf("\r\n==== fire-f429 LTDC touch test ====\r\n");
    printf("LCD: %ux%u RGB888, FB at 0x%08lX\r\n",
           (unsigned)SCREEN_W, (unsigned)SCREEN_H,
           (unsigned long)LTDC_Display_FrameBuffer());

    if (Touch_ReadVersion(touch_version))
    {
        printf("Touch PID 0x%02X%02X (GT91x), version 0x%02X, sensor 0x%02X\r\n",
               touch_version[0], touch_version[1],
               touch_version[2], touch_version[3]);
        printf("Touch: %s config (800x480)\r\n",
               Touch_LoadConfig() == 0 ? "loaded" : "FAILED");
    }
    else
    {
        printf("Touch: no ACK on bit-banged I2C2 (PH4/PH5)\r\n");
    }

    /* Isolation step: solid full-screen red for a few seconds. */
    LTDC_Clear(COL_RED);
    printf("Solid RED screen shown for 3 s\r\n");
    HAL_Delay(3000);

    while (1)
    {
        shapes_demo(12000);
        colors_demo(1200);
        gradient_demo(6000);
        shapes_demo(12000);
    }
}
