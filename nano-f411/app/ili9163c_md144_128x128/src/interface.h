/*
  interface.h - low-level ILI9163C bus primitives (nano-f411 port).

  The module connector has NO D/C pin: the panel is strapped for 3-wire
  serial, where every byte is a 9-bit frame - the D/C bit (0 = command,
  1 = data) is clocked first, then the 8 data bits, MSB first. CS frames
  each command; data bursts hold CS low across bytes.

  Two drive methods, selected at runtime:

    SOFT: PA5/PA7 bit-banged, one 9-bit frame per byte.
    HW  : SPI1 AF5 (PA5 = SCK, PA7 = MOSI), mode 3, **16-bit frames
          carrying a packed 9-bit-frame bitstream** (the F411 SPI cannot
          produce native 9-bit frames - DFF is 8/16-bit only). The panel
          only sees SCL/SDA and counts its own 9-bit boundaries, so the
          packed stream is transparent to it. Leftover bits before CS
          rises are always < 9, so no extra frame can complete.
          APB2 = 100 MHz (project clock override): default prescaler
          /8 = 12.5 MHz SCK (in-spec for the panel); /4 = 25 MHz available
          via LCD_SPI1_PRESC.

    SCL = PA5, SDA = PA7 (soft GPIO or SPI1 AF5)
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

/* ---- bus selection (SOFT bit-bang vs HW SPI1) ---- */
void    LCD_UseSoftBus(void);   /* PA5/PA7 re-muxed to GPIO               */
void    LCD_UseHwBus(void);     /* PA5/PA7 re-muxed to SPI1 AF5 + init    */
uint8_t LCD_BusIsHw(void);      /* 1 while the HW SPI1 bus is selected    */
unsigned long LCD_HwSpiKHz(void); /* active SPI1 baud in kHz (info page)  */
void    SPI_HW_Flush(void);     /* drain the HW TX buffer (blocking)      */

#endif /* __INTERFACE_H */