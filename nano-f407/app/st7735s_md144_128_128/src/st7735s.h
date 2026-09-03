#ifndef NANO_F407_ST7735S_H
#define NANO_F407_ST7735S_H

#include <stdint.h>

#define LCD_WIDTH   128
#define LCD_HEIGHT  128

#define ST7735_BLACK    0x0000
#define ST7735_WHITE    0xFFFF
#define ST7735_RED      0xF800
#define ST7735_GREEN    0x07E0
#define ST7735_BLUE     0x001F
#define ST7735_CYAN     0x07FF
#define ST7735_MAGENTA  0xF81F
#define ST7735_YELLOW   0xFFE0
#define ST7735_ORANGE   0xFD20

/* r,g,b in 0..255 -> 16-bit RGB565 */
#define ST7735_RGB565(r,g,b) \
    ((uint16_t)((((uint16_t)(r) & 0xF8u) << 8) | \
                (((uint16_t)(g) & 0xFCu) << 3)  | \
                (((uint16_t)(b) & 0xF8u) >> 3)))

void ST7735_Init(void);
void ST7735_Backlight_Init(void);   /* PE9 = TIM1_CH1 PWM @ 25% */
void ST7735_Fill(uint16_t color);
void ST7735_DrawPixel(uint16_t x, uint16_t y, uint16_t color);
void ST7735_FillRect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);
void ST7735_BlitFB(const uint16_t *fb);   /* blit a full LCD_WIDTH x LCD_HEIGHT RGB565 buffer */

#endif /* NANO_F407_ST7735S_H */
