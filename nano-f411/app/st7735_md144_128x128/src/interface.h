/*
  interface.h - low-level ST7735S bus primitives (nano-f411 port).
  One wiring (SCL=PA5, SDA=PA7), two drive methods selected at runtime:

    SOFT: PA5/PA7 re-muxed to plain GPIO outputs and bit-banged (the
          proven path of this project).
    HW  : PA5/PA7 re-muxed to SPI1 AF5 (SCK / MOSI), mode 3 (CPOL=1
          CPHA=1 - same idle-high / rising-edge sampling as the bit-bang),
          MSB first, 8-bit, software NSS. CS=PB8 and DC=PA4 stay GPIO.
          APB2 = 50 MHz -> the default prescaler /4 gives 12.5 MHz
          (in-spec for ST7735S); raise to /2 = 25 MHz via LCD_SPI1_PRESC
          if the module tolerates it.

  Pin roles:
    SCL = PA5  (SPI clock, bit-banged or SPI1_SCK)
    SDA = PA7  (SPI MOSI, bit-banged or SPI1_MOSI; swapped from the
                vendor PA6 which was SPI1_MISO - useless here)
    DC  = PA4  (data/command, always GPIO)
    RES = PA6  (reset, always GPIO; SPI1_MISO unused by the write-only
                panel)
    CS  = PB8  (chip select, always GPIO)
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

/* ---- bus selection (SOFT bit-bang vs HW SPI1) ---- */
void    LCD_UseSoftBus(void);   /* re-mux PA5/PA7 to GPIO, idle high      */
void    LCD_UseHwBus(void);     /* re-mux PA5/PA7 to SPI1 AF5 + init      */
uint8_t LCD_BusIsHw(void);      /* 1 while the HW SPI1 bus is selected    */
unsigned long LCD_HwSpiKHz(void); /* active SPI1 baud in kHz (info page)  */
void    SPI_HW_SetSpeed(void);  /* (re)apply the configured SPI prescaler */
void    SPI_HW_Flush(void);     /* drain the HW TX buffer (blocking)      */

#endif /* __INTERFACE_H */