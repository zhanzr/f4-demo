/*
  interface.h - low-level ST7365P bus primitives (nano-f411 port, soft SPI).

  Standard 4-wire serial: CS frames each transfer, DC selects command vs
  data, SCL idles high and the panel latches SDA on the rising edge.

    SCL = PA5  (clock, bit-banged)
    SDA = PA7  (data,   bit-banged)
    DC  = PA4  (data/command, always GPIO)
    RES = PA3  (reset, always GPIO)
    CS  = PB8  (chip select, always GPIO)
    BL  = PB9  (backlight, TIM4_CH4 PWM via backlight.c)
    MISO = PA6 (module read-back line; not used by this write-only driver)

  A future HW SPI1 path maps SCL/SDA/MISO to SPI1_SCK/MOSI/MISO (AF5).
*/

#ifndef __INTERFACE_H
#define __INTERFACE_H

#include <stdint.h>

void CS_SET(void);
void CS_CLR(void);
void WriteComm(uint16_t data);
void WriteData(uint16_t data);
void SendData(uint32_t color);
void LCD_WriteDataFast(uint8_t data);   /* raw byte, caller manages CS/DC */
void LCD_BeginData(void);                /* DC high, CS low, for raster bursts */
void LCD_EndData(void);                  /* CS high after a burst */

#endif /* __INTERFACE_H */