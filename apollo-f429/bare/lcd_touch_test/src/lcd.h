#ifndef __LCD_H
#define __LCD_H

/*
  lcd.h - ILI9341 2.8" TFTLCD on a 16-bit FSMC parallel bus (apollo-f429 port).

  Two API layers, deliberately:
    - vendor ALIENTEK style (used by the vendored touch stack):
        POINT_COLOR / BACK_COLOR globals, LCD_Init/LCD_Clear(color),
        LCD_ShowString/LCD_ShowNum/LCD_ShowxNum, LCD_DrawPoint/LCD_DrawLine/
        LCD_Draw_Circle, LCD_Fill, LCD_Color_Fill, LCD_Set_Window,
        LCD_WriteRAM_Prepare/LCD_WriteRAM, LCD_Fast_DrawPoint, LCD_ReadPoint,
        LCD_WriteReg/LCD_ReadReg, LCD_Display_Dir/Scan_Dir.
    - st7789-style drawing API (used by the test patterns, ported from the
      h723-mini / nano-f411 LCD ports): LCD_SetColor/LCD_SetBackColor,
      LCD_ClearBg()/LCD_ClearRect/LCD_FillRect/LCD_CopyBuffer and the
      LCD_DrawPoint(x,y,c)/LCD_DrawLine_V+H+XY/Rect/Circle/FillCircle primitives,
      LCD_SetAsciiFont/LCD_ShowTransparent/LCD_DisplayChar/LCD_DisplayString,
      plus BlockWrite + the vendor Disp* TEST_STAND screens.

  Both layers share the same FSMC window write primitives, so they never
  conflict.
*/

#include <stdint.h>
#include "stm32f4xx_hal.h"
#include "fonts/lcd_fonts.h"

/* ---- panel geometry (ILI9341 2.8" 240x320) ---- */
#define LCD_Width    240
#define LCD_Height   320
#define COL       240
#define ROW       320
#define COL_Pre   0
#define ROW_Pre   0
#define Delay_Time 500

/* ---- FSMC parallel bus (bank1 NE1, 16-bit, A18=RS) ---- */
typedef struct
{
    __IO uint16_t LCD_REG;
    __IO uint16_t LCD_RAM;
} LCD_TypeDef;
#define LCD_BASE   ((uint32_t)(0x60000000UL | 0x0007FFFEUL))
#define LCD        ((LCD_TypeDef *)LCD_BASE)

/* ---- raw RGB565 ---*/
#define WHITE   0xFFFF
#define BLACK   0x0000
#define BLUE    0x001F
#define BRED    0xF81F
#define GRED    0xFFE0
#define GBLUE   0x07FF
#define RED     0xF800
#define MAGENTA 0xF81F
#define GREEN   0x07E0
#define CYAN    0x7FFF
#define YELLOW  0xFFE0
#define BROWN   0xBC40
#define BRRED   0xFC07
#define GRAY    0x8430
#define LGRAY   0xC618

#define DARKBLUE  0x01CF
#define LIGHTBLUE 0x7D7C
#define GRAYBLUE  0x5458
#define LIGHTGREEN 0x841F
#define LBBLUE    0x2B12

/* ---- 24-bit colors (RGB888 -> RGB565 in LCD_SetColor) ---- */
#define LCD_WHITE   0xFFFFFF
#define LCD_BLACK   0x000000
#define LCD_BLUE    0x0000FF
#define LCD_GREEN   0x00FF00
#define LCD_RED     0xFF0000
#define LCD_CYAN    0x00FFFF
#define LCD_MAGENTA 0xFF00FF
#define LCD_YELLOW  0xFFFF00

#define ABS(X)  ((X) > 0 ? (X) : -(X))

/* ---- scan directions (vendor lcd.h) ---- */
#define L2R_U2D  0
#define L2R_D2U  1
#define R2L_U2D  2
#define R2L_D2U  3
#define U2D_L2R  4
#define U2D_R2L  5
#define D2U_L2R  6
#define D2U_R2L  7
#define DFT_SCAN_DIR  L2R_U2D

/* ---- vendor lcd device ---- */
typedef struct
{
    uint16_t width;         /* LCD width   */
    uint16_t height;        /* LCD height  */
    uint16_t id;            /* LCD IC id (0x9341 for ILI9341) */
    uint8_t  dir;           /* 0 portrait, 1 landscape */
    uint16_t wramcmd;       /* 0x2C */
    uint16_t setxcmd;       /* 0x2A */
    uint16_t setycmd;       /* 0x2B */
} lcd_dev_t;

extern lcd_dev_t  lcddev;
extern uint32_t   POINT_COLOR;
extern uint32_t   BACK_COLOR;

/* =====================================================================
   Vendor-style init / registers / cursor
   ===================================================================== */
void    LCD_Init(void);
void    LCD_WriteReg(uint16_t reg, uint16_t val);
uint16_t LCD_ReadReg(uint16_t reg);
void    LCD_Set_Window(uint16_t sx, uint16_t sy, uint16_t w, uint16_t h);
void    LCD_SetCursor(uint16_t x, uint16_t y);
void    LCD_WriteRAM_Prepare(void);
void    LCD_WriteRAM(uint16_t rgb);
void    LCD_Display_Dir(uint8_t dir);
void    LCD_Scan_Dir(uint8_t dir);

void    LCD_Clear(uint32_t color);
void    LCD_Fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint32_t color);
void    LCD_Color_Fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color);
void    LCD_DrawPoint(uint16_t x, uint16_t y);
void    LCD_Fast_DrawPoint(uint16_t x, uint16_t y, uint32_t color);
uint32_t LCD_ReadPoint(uint16_t x, uint16_t y);
void    LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void    LCD_DrawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void    LCD_Draw_Circle(uint16_t x0, uint16_t y0, uint8_t r);
void    LCD_ShowChar(uint16_t x, uint16_t y, uint8_t num, uint8_t size, uint8_t mode);
void    LCD_ShowNum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size);
void    LCD_ShowxNum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint8_t mode);
void    LCD_ShowString(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                       uint8_t size, uint8_t *p);

/* =====================================================================
   st7789-style drawing API (pattern port)
   ===================================================================== */
void LCD_SetAsciiFont(pFONT *font);
void LCD_ShowTransparent(uint8_t mode);
void LCD_DisplayChar(uint16_t x, uint16_t y, uint8_t c);
void LCD_DisplayString(uint16_t x, uint16_t y, char *p);

void LCD_SetColor(uint32_t rgb888);
void LCD_SetBackColor(uint32_t rgb888);
void LCD_SetAddress(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void LCD_ClearBg(void);      /* clear whole panel with s_BackColor */
void LCD_ClearRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
void LCD_FillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
void LCD_DrawPointC(uint16_t x, uint16_t y, uint32_t color);
void LCD_DrawLine_H(uint16_t x, uint16_t y, uint16_t width);
void LCD_DrawLine_V(uint16_t x, uint16_t y, uint16_t height);
void LCD_DrawLineXY(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void LCD_DrawRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
void LCD_DrawCircle(uint16_t x, uint16_t y, uint16_t r);
void LCD_FillCircle(uint16_t x, uint16_t y, uint16_t r);
void LCD_CopyBuffer(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                    uint16_t *data);

/* vendor TEST_STAND helper screens */
void BlockWrite(uint16_t Xstart, uint16_t Xend, uint16_t Ystart, uint16_t Yend);
void DispColor(uint32_t color);
void DispFrame(void);
void DispGrayHor16(void);
void DispBand(void);
void StopDelay(uint16_t ms);

#endif /* __LCD_H */