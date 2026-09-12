/*
  interface.h - low-level NV3030B bus primitives (nano-f411 port).

  Hardware SPI1 only (the vendor example likewise drives the panel from
  the SPI peripheral - there is no software bit-bang in it):

    HW: PA5/PA7 re-muxed to SPI1 AF5 (SCK/MOSI), mode 3 (CPOL=1, CPHA=1
        - the same idle-high / rising-edge-sampling timing the vendor's
        hard-SPI uses), 8-bit, MSB first, NSS soft. APB2 = 50 MHz
        (board default clock tree): default prescaler /4 = 12.5 MHz SCK
        to isolate bring-up; /2 = 25 MHz via LCD_SPI1_PRESC once
        verified.

  NV3030B wrapped-command framing (vendor-verbatim): every command is
  written as CS high (settle), CS low, then four bytes 02 00 <cmd> 00;
  parameter and pixel bytes then stream into the same CS frame. The
  module's DC pin is not part of this protocol (the vendor never drives
  it), and there is no reset pin - power-cycling the module is the only
  recovery from a latched state.

    SCL = PA5, SDA/MOSI = PA7, CS = PA4 (GPIO).
    DC  = PA6  - NOT USED (vendor defines it but never drives it; the
                 wrapped format carries command/data framing).
    MISO - not exposed on this module connector; the panel is write-only
    through this interface.
*/

#ifndef __INTERFACE_H
#define __INTERFACE_H

#include <stdint.h>

void CS_SET(void);
void CS_CLR(void);
void WriteComm(uint16_t data);
void WriteData(uint16_t data);
void SendData(uint32_t color);
void LCD_WriteDataFast(uint8_t data);   /* raw byte, caller manages framing */
void LCD_BeginData(void);                /* CS low, ready for raster bytes */
void LCD_EndData(void);                  /* CS high, closes the frame */

/* ---- HW SPI1 bus ---- */
void    LCD_UseHwBus(void);     /* mux PA5/PA7 to SPI1 AF5 + init         */
uint8_t LCD_BusIsHw(void);      /* always 1 (HW-only build)               */
unsigned long LCD_HwSpiKHz(void); /* active SPI1 baud in kHz (info page)  */
void    SPI_HW_Flush(void);     /* drain the HW TX buffer (blocking)      */

#endif /* __INTERFACE_H */