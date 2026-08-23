#include <stdio.h>
#include "board.h"
#include "board_ltdc.h"

/* Per-project visual identity: bare uses a blue theme, app uses a green
 * theme so the two builds are immediately distinguishable on screen. */
#ifdef DATA_IN_ExtSDRAM
#define THEME_BG     0x00260C1DU /* deep green */
#define THEME_ACCENT 0x0000E676U /* bright green */
#define THEME_TITLE  "SDRAM app"
#else
#define THEME_BG     0x00000A1AU /* deep blue */
#define THEME_ACCENT 0x000091EAU /* bright blue */
#define THEME_TITLE  "bare (SRAM app data, SDRAM fb)"
#endif

#define FPS_BAND 32U

typedef struct
{
    int x, y, vx, vy, size;
    uint32_t color;
} shape_t;

static const uint32_t palette[] = {
    0x00E53935U, 0x00FB8C00U, 0x00FDD835U, 0x0043A047U,
    0x001E88E5U, 0x008E24AAU, 0x00D81B60U, 0x0000BCD4U,
};

static void DrawShapes(shape_t *shapes, int count, uint16_t anim_h)
{
    for (int i = 0; i < count; i++)
    {
        shapes[i].x += shapes[i].vx;
        shapes[i].y += shapes[i].vy;
        if (shapes[i].x - shapes[i].size < 0 || shapes[i].x + shapes[i].size > (int)LCD_WIDTH)
        {
            shapes[i].vx = -shapes[i].vx;
        }
        if (shapes[i].y - shapes[i].size < 0 || shapes[i].y + shapes[i].size > (int)anim_h)
        {
            shapes[i].vy = -shapes[i].vy;
        }
        LTDC_FillRect((uint16_t)(shapes[i].x - shapes[i].size),
                      (uint16_t)(shapes[i].y - shapes[i].size),
                      (uint16_t)(shapes[i].size * 2),
                      (uint16_t)(shapes[i].size * 2),
                      shapes[i].color);
    }
}

int main(void)
{
    uint32_t frames = 0;
    uint32_t last_tick = 0;
    uint32_t last_x = 0xFFFF;

    HAL_Init();
    Board_Init();
    LTDC_Display_Init();
    Touch_Init();

    const uint16_t anim_h = (uint16_t)(LCD_HEIGHT - FPS_BAND);
    static shape_t shapes[8];
    for (int i = 0; i < 8; i++)
    {
        shapes[i].x = 60 + (i * 97) % (LCD_WIDTH - 120);
        shapes[i].y = 40 + (i * 53) % (anim_h - 80);
        shapes[i].vx = (i % 2 ? 4 : -4) + (i % 3);
        shapes[i].vy = (i % 3 ? 3 : -3) + (i % 4);
        shapes[i].size = 18 + (i % 4) * 7;
        shapes[i].color = palette[i % 8];
    }

    printf("\r\n==== fire-f429 LTDC touch test (%s) ====\r\n", THEME_TITLE);
    printf("LCD: 800x480 RGB888, FB at 0x%08lX\r\n",
           (unsigned long)LTDC_Display_FrameBuffer());

    /* Isolation step: solid full-screen red, held for a few seconds, so it is
     * easy to tell whether the panel (backlight + timing) is working at all. */
    LTDC_Clear(0x00FF0000U);
    printf("Solid RED screen shown for 5 s - is the panel visible?\r\n");
    HAL_Delay(5000);

    while (1)
    {
        LTDC_Clear(THEME_BG);
        DrawShapes(shapes, 8, anim_h);

        TouchPoint t = Touch_Scan();
        if (t.pressed)
        {
            uint16_t cx = t.x < 14 ? 14 : (t.x > LCD_WIDTH - 15 ? LCD_WIDTH - 15 : t.x);
            uint16_t cy = t.y < 14 ? 14 : (t.y > LCD_HEIGHT - 15 ? LCD_HEIGHT - 15 : t.y);
            LTDC_FillRect((uint16_t)(cx - 14), (uint16_t)(cy - 14), 28, 28, 0x00FFFFFFU);
            LTDC_FillRect((uint16_t)(cx - 8), (uint16_t)(cy - 8), 16, 16, THEME_ACCENT);
            if (t.x != last_x)
            {
                printf("Touch: X=%u Y=%u\r\n", (unsigned)t.x, (unsigned)t.y);
                last_x = t.x;
            }
        }
        else
        {
            last_x = 0xFFFF;
        }

        LTDC_FillRect(0, anim_h, LCD_WIDTH, FPS_BAND, THEME_ACCENT);
        frames++;

        uint32_t now = HAL_GetTick();
        if (now - last_tick >= 2000U)
        {
            uint32_t fps = (frames * 1000U) / (now - last_tick);
            frames = 0;
            last_tick = now;
            printf("FPS: %lu  |  fills/s ~ %lu  (%s)\r\n",
                   (unsigned long)fps,
                   (unsigned long)(fps * 9U),
                   THEME_TITLE);
        }
    }
}
