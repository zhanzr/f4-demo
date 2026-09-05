/*
  lcd.h - ST7735S 1.44" 128x128 LCD driver (nano-f411 port).
  Mirrors the vendor example C8T6_md144_t1 (HARDWARE/lcd/lcd.h), trimmed to the
  4-wire SPI interface and the demo API used by this project.
    SCL = PA5, SDA = PA6, RES = PA7, DC = PA4, CS = PB8, BL = PB9
  Display config: ST7735S, 128x128, MADCTL 0xC8 (1.44", vertical offset 32),
  16-bit RGB565.
*/

#ifndef __LCD_H
#define __LCD_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

/* ---- Panel geometry (vendor main.h) ---- */
#define COL       128
#define ROW       128
#define COL_Pre   0
#define ROW_Pre   32
#define Delay_Time 500

/* ---- 4-wire SPI control pins ---- */
#define LCD_GPIO_PortSCL    GPIOA
#define LCD_SCL_Pin         GPIO_PIN_5
#define LCD_GPIO_PortSDA    GPIOA
#define LCD_SDA_Pin         GPIO_PIN_6
#define LCD_GPIO_PortRS     GPIOA
#define LCD_RS_Pin          GPIO_PIN_4   /* DC  */
#define LCD_GPIO_PortRST    GPIOA
#define LCD_RST_Pin         GPIO_PIN_7
#define LCD_GPIO_PortCS     GPIOB
#define LCD_CS_Pin          GPIO_PIN_8
#define LCD_GPIO_PortBL     GPIOB
#define LCD_BL_Pin          GPIO_PIN_9   /* TIM4_CH4 backlight PWM */

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

/* ---- colors ---- */
#define WHITE   0xFFFF
#define BLACK   0x0000
#define BLUE    0x001F
#define RED     0xF800
#define MAGENTA 0xF81F
#define GREEN   0x07E0
#define CYAN    0x7FFF
#define YELLOW  0xFFE0

/* ---- API ---- */
void LCD_GPIOInit(void);
void LCD_IC_Init(void);
void LCD_Init(void);
void BlockWrite(uint16_t Xstart, uint16_t Xend, uint16_t Ystart, uint16_t Yend);
void DispColor(uint32_t color);
void DispFrame(void);
void DispGrayHor16(void);
void DispBand(void);
void StopDelay(uint16_t ms);

#endif /* __LCD_H */