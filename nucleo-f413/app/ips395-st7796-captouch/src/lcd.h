/*
  lcd.h - ST7796S 4.0" 320x480 IPS LCD driver (nucleo-f413 port,
  CL40BC267-40A / 40002 module). Same drawing API as the other LCD
  projects in this repo (24-bit colors, lines, rectangles, circles,
  fills, buffer copy, ASCII text).
  Pins: SCL=PA5, SDA=PA7 (SPI1 AF5 in HW mode / bit-bang in soft mode),
  DC=PD15, RST=PF12, CS=PD14, BL=PE9 (TIM1_CH1 PWM), MISO=PA6 (panel
  SDO, routed on this module's header).
  Panel geometry: 320x480, windows at COL_Pre = 0, ROW_Pre = 0 (the
  controller addresses the full range directly), MADCTL 0x48 (LCD_MADCTL
  knob), 16bpp RGB565 - two bytes per pixel even in 4-line serial.
*/

#ifndef __LCD_H
#define __LCD_H

#include <stdint.h>
#include "stm32f4xx_hal.h"
#include "lcd/lcd_fonts.h"

/* ---- Panel geometry / demo timing ---- */
#define LCD_Width    320
#define LCD_Height   480
#define COL       320
#define ROW       480
#define COL_Pre   0
#define ROW_Pre   0
#define Delay_Time 500

/* ---- runtime drawing window ----
 * All drawing is relative to this window; it defaults to the full panel
 * and can be shrunk (e.g. to a centered 256x224 NES window) at runtime. */
void LCD_SetWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void LCD_ResetWindow(void);   /* back to the full panel */
uint16_t LCD_W(void);         /* current window width  */
uint16_t LCD_H(void);         /* current window height */

/* ---- 4-wire SPI control pins ---- */
#define LCD_GPIO_PortSCL    GPIOA
#define LCD_SCL_Pin         GPIO_PIN_5
#define LCD_GPIO_PortSDA    GPIOA
#define LCD_SDA_Pin         GPIO_PIN_7
#define LCD_GPIO_PortRS     GPIOD
#define LCD_RS_Pin          GPIO_PIN_15  /* DC  */
#define LCD_GPIO_PortRST    GPIOF
#define LCD_RST_Pin         GPIO_PIN_12
#define LCD_GPIO_PortCS     GPIOD
#define LCD_CS_Pin          GPIO_PIN_14
#define LCD_GPIO_PortBL     GPIOE
#define LCD_BL_Pin          GPIO_PIN_9   /* TIM1_CH1 backlight PWM */
#define LCD_GPIO_PortMISO   GPIOA
#define LCD_MISO_Pin        GPIO_PIN_6   /* panel SDO, wired for read-back */

/* ---- reserved for the FT6336U capacitive touch (not driven yet) ----------
 * T_INT = PG2 (EXTI2, active low), T_RST = PG3 (active low),
 * T_SCL = PB8 (I2C1_SCL, AF4),   T_SDA = PB9 (I2C1_SDA, AF4).
 * The module carries 10K pull-ups on SDA/SCL; slave address 0x38. */
#define TOUCH_INT_Pin       GPIO_PIN_2
#define TOUCH_INT_GPIO_Port GPIOG
#define TOUCH_RST_Pin       GPIO_PIN_3
#define TOUCH_RST_GPIO_Port GPIOG
#define TOUCH_SCL_Pin       GPIO_PIN_8
#define TOUCH_SCL_GPIO_Port GPIOB
#define TOUCH_SDA_Pin       GPIO_PIN_9
#define TOUCH_SDA_GPIO_Port GPIOB

/* ---- pin accessors (bit-banged SPI) ---- */
#define LCD_SPI_SCL_SET  HAL_GPIO_WritePin(LCD_GPIO_PortSCL, LCD_SCL_Pin, GPIO_PIN_SET)
#define LCD_SPI_SCL_CLR  HAL_GPIO_WritePin(LCD_GPIO_PortSCL, LCD_SCL_Pin, GPIO_PIN_RESET)
#define LCD_SPI_SDA_SET  HAL_GPIO_WritePin(LCD_GPIO_PortSDA, LCD_SDA_Pin, GPIO_PIN_SET)
#define LCD_SPI_SDA_CLR  HAL_GPIO_WritePin(LCD_GPIO_PortSDA, LCD_SDA_Pin, GPIO_PIN_RESET)
#define LCD_RS_SET       HAL_GPIO_WritePin(LCD_GPIO_PortRS,  LCD_RS_Pin,  GPIO_PIN_SET)
#define LCD_RS_CLR       HAL_GPIO_WritePin(LCD_GPIO_PortRS,  LCD_RS_Pin,  GPIO_PIN_RESET)
#define LCD_RST_SET      HAL_GPIO_WritePin(LCD_GPIO_PortRST, LCD_RST_Pin, GPIO_PIN_SET)
#define LCD_RST_CLR      HAL_GPIO_WritePin(LCD_GPIO_PortRST, LCD_RST_Pin, GPIO_PIN_RESET)
#define LCD_CS_SET       HAL_GPIO_WritePin(LCD_GPIO_PortCS,  LCD_CS_Pin,  GPIO_PIN_SET)
#define LCD_CS_CLR       HAL_GPIO_WritePin(LCD_GPIO_PortCS,  LCD_CS_Pin,  GPIO_PIN_RESET)

/* ---- 24-bit colors (RGB888, converted to RGB565 by LCD_SetColor) ---- */
#define LCD_WHITE       0xFFFFFF
#define LCD_BLACK       0x000000
#define LCD_BLUE        0x0000FF
#define LCD_GREEN       0x00FF00
#define LCD_RED         0xFF0000
#define LCD_CYAN        0x00FFFF
#define LCD_MAGENTA     0xFF00FF
#define LCD_YELLOW      0xFFFF00

/* ---- raw RGB565 colors (vendor screens use these directly) ---- */
#define WHITE   0xFFFF
#define BLACK   0x0000
#define BLUE    0x001F
#define RED     0xF800
#define MAGENTA 0xF81F
#define GREEN   0x07E0
#define CYAN    0x7FFF
#define YELLOW  0xFFE0

/* MADCTL (0x36): vendor portrait = 0x48 (MX + RGB). Retune with
 * -DLCD_MADCTL=0x.. if the image is mirrored/rotated. */
#ifndef LCD_MADCTL
#define LCD_MADCTL  0x48
#endif

#define ABS(X)  ((X) > 0 ? (X) : -(X))

/* ---- API: init / window / colors ---- */
void LCD_GPIOInit(void);
void LCD_RESET(void);
void LCD_IC_Init(void);
void LCD_Init(void);
void LCD_Reinit(void);   /* reset + re-init after a bus switch */
void LCD_SetAddress(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void LCD_SetColor(uint32_t rbg888);
void LCD_SetBackColor(uint32_t rbg888);
void LCD_Clear(void);
void LCD_ClearRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height);

/* ---- API: vendor screen-window helper (raw, absolute coords) ---- */
void BlockWrite(uint16_t Xstart, uint16_t Xend, uint16_t Ystart, uint16_t Yend);

/* ---- API: vendor demo screens ---- */
void DispColor(uint32_t color);
void DispFrame(void);
void DispGrayHor16(void);
void DispBand(void);
void StopDelay(uint16_t ms);

/* ---- API: ASCII text ---- */
void LCD_SetAsciiFont(pFONT *font);
void LCD_ShowTransparent(uint8_t mode);
void LCD_DisplayChar(uint16_t x, uint16_t y, uint8_t c);
void LCD_DisplayString(uint16_t x, uint16_t y, char *p);

/* ---- single-pass page renderer -------------------------------------------
 * Paints the window background and all text lines into one continuous
 * raster stream (one BeginData/EndData pass for the whole page). */
typedef struct
{
    const char *text;    /* NUL-terminated ASCII string           */
    uint16_t    x, y;    /* top-left pixel of the text block      */
    uint16_t    scale;   /* 1 = font pixels, 2 = doubled          */
} LcdTextLine;

void LCD_DrawTextPage(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      uint8_t nlines, const LcdTextLine *lines);
void LCD_DisplayCharScaled(uint16_t x, uint16_t y, uint8_t c, uint16_t scale);
void LCD_DisplayStringScaled(uint16_t x, uint16_t y, char *p, uint16_t scale);

/* ---- API: 2D drawing ---- */
void LCD_DrawPoint(uint16_t x, uint16_t y, uint32_t color);
void LCD_DrawLine_V(uint16_t x, uint16_t y, uint16_t height);
void LCD_DrawLine_H(uint16_t x, uint16_t y, uint16_t width);
void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void LCD_DrawRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
void LCD_DrawCircle(uint16_t x, uint16_t y, uint16_t r);
void LCD_FillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
void LCD_FillCircle(uint16_t x, uint16_t y, uint16_t r);

/* ---- API: raw buffer blit (RGB565 words) ---- */
void LCD_CopyBuffer(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                    uint16_t *data);

#endif /* __LCD_H */