/*
  interface.h - low-level ILI9163C bus primitives (nano-f411 port, soft SPI).

  The module connector has NO D/C pin: the panel is strapped for 3-wire
  serial, where every byte is a 9-bit frame - the D/C bit (0 = command,
  1 = data) is clocked first, then the 8 data bits, MSB first. CS frames
  each command; data bursts hold CS low across bytes.

    SCL = PA5  (clock, bit-banged)
    SDA = PA7  (data,   bit-banged)
    RES = PA6  (reset, always GPIO)
    CS  = PB8  (chip select, always GPIO)

  The module's backlight is hardwired on-board (always on after power).
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
void LCD_BeginData(void);                /* CS low, for raster bursts */
void LCD_EndData(void);                  /* CS high after a burst */

#endif /* __INTERFACE_H */