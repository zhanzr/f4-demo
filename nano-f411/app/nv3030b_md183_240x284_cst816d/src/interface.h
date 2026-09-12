/*
  interface.h - low-level NV3030B bus primitives (nano-f411 port).

  Two transports behind one byte-level API:

    SOFT: PA5/PA7 bit-banged like the vendor's TK499 soft-SPI example
          (clock low, set SDA, clock high), ~2 MHz.
    HW:   PA5/PA7 on SPI1 AF5, mode 3 (CPOL=1, CPHA=2EDGE - the vendor's
          F103 hard-SPI timing), 8-bit MSB first, NSS soft. APB2 = 50 MHz
          (board default clock tree); prescaler is a build knob
          (LCD_SPI1_PRESC).

  NV3030B wrapped-command framing (vendor-verbatim): every command is
  written as CS high (settle), CS low, then four bytes 02 00 <cmd> 00;
  parameter and pixel bytes then stream into the same CS frame. The
  module's DC pin (PA6) is driven push-pull LOW (as the vendor leaves
  it) but never toggled. There is no reset pin - power-cycling the
  module is the only recovery from a latched state.

    SCL = PA5, SDA/MOSI = PA7, CS = PA4 (GPIO).
    DC  = PA6 (driven low, not part of the wrapped protocol).
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

/* ---- bus selection ---- */
void    LCD_UseHwBus(void);     /* mux PA5/PA7 to SPI1 AF5 + init         */
void    LCD_UseSoftBus(void);   /* PA5/PA7 as GPIO outputs, bit-bang      */
uint8_t LCD_BusIsHw(void);      /* 1 = SPI1, 0 = soft bit-bang            */
unsigned long LCD_HwSpiKHz(void); /* active SPI1 baud in kHz (info page)  */
unsigned long LCD_SoftKHz(void);  /* approx soft-bus rate in kHz          */
void    SPI_HW_Flush(void);     /* drain the HW TX buffer (blocking)      */

#endif /* __INTERFACE_H */