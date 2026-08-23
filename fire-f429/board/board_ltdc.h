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
void Touch_Init(void);
TouchPoint Touch_Scan(void);

#endif
