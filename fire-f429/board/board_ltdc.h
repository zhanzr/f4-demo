#ifndef FIRE_F429_BOARD_LTDC_H
#define FIRE_F429_BOARD_LTDC_H

#include <stdint.h>

#define LCD_WIDTH  800U
#define LCD_HEIGHT 480U

typedef struct
{
    uint16_t x;
    uint16_t y;
    uint8_t pressed;
} TouchPoint;

void LTDC_Display_Init(void);
uint32_t LTDC_Display_FrameBuffer(void);
void LTDC_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color);
void LTDC_Clear(uint32_t color);
void LTDC_DrawLine(int x0, int y0, int x1, int y1, uint32_t color);
void LTDC_FillCircle(int cx, int cy, int r, uint32_t color);
void LTDC_DrawString(uint16_t x, uint16_t y, const char *str,
                     uint32_t fg_color, uint32_t bg_color);
void Touch_Init(void);
int Touch_ReadVersion(uint8_t version[4]);
int Touch_LoadConfig(void);
int Touch_Probe(int int_high);
TouchPoint Touch_Scan(void);

#endif
