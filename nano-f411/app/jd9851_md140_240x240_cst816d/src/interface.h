/*
  interface.h - low-level JD9851 bus primitives (nano-f411 port).

  Hardware SPI1 only (the vendor example likewise drives the panel from
  the SPI peripheral): PA5/PA7 on SPI1 AF5, mode 3 (CPOL=1, CPHA=2EDGE -
  the vendor's F103 hard-SPI timing), 8-bit MSB first, NSS soft.
  APB2 = 100 MHz (project clock override, main.c); the prescaler is a
  build knob (LCD_SPI1_PRESC).

  JD9851 framing (vendor-verbatim): plain DC-based 4-wire SPI - the
  command byte goes out with DC(RS) low, every data byte with DC high.
  No reset pin - power-cycling the module is the only recovery from a
  latched state.

    SCL = PA5, SDA/MOSI = PA7, CS = PA4, DC(RS) = PA6 (GPIO).
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
void LCD_FillBulk(uint32_t color, uint32_t pixels); /* solid burst, open frame */
void LCD_BeginData(void);                /* CS low + DC high, raster ready  */
void LCD_EndData(void);                  /* CS high, closes the frame       */

/* ---- HW SPI1 bus ---- */
void    LCD_UseHwBus(void);     /* mux PA5/PA7 to SPI1 AF5 + init         */
uint8_t LCD_BusIsHw(void);      /* always 1 (HW-only build)               */
unsigned long LCD_HwSpiKHz(void); /* active SPI1 baud in kHz (info page)  */
void    SPI_HW_Flush(void);     /* drain the HW TX buffer (blocking)      */

#endif /* __INTERFACE_H */
