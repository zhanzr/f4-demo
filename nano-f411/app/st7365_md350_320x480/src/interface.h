/*
  interface.h - low-level ST7365P bus primitives (nano-f411 port).

  Standard 4-wire serial: CS frames each transfer, DC selects command vs
  data, the panel latches SDA on the rising edge. Two drive methods,
  selected at runtime:

    SOFT: PA5/PA7 bit-banged (idle-high SCL).
    HW  : SPI1 AF5 (PA5 = SCK, PA7 = MOSI), mode 3 (CPOL=1, CPHA=1 - the
          same idle-high / rising-edge-sampling timing the bit-bang
          produces), 8-bit, MSB first, NSS soft. APB2 = 100 MHz (project
          clock override): default prescaler /2 = **50 MHz SCK** (the
          F411 SPI1 max); /4 = 25 MHz available via LCD_SPI1_PRESC.
          HW raster bursts stream through a 512-byte TX buffer (one
          HAL_SPI_Transmit per <=512 bytes).

    SCL = PA5, SDA = PA7 (soft GPIO or SPI1 AF5)
    DC  = PA4  (data/command, always GPIO)
    RES = PA3  (reset, always GPIO)
    CS  = PB8  (chip select, always GPIO)
    BL  = PB9  (backlight, TIM4_CH4 PWM via backlight.c)
    MISO = PA6 (module read-back line; not used by this TX-only driver -
                maps to SPI1_MISO at AF5 for full-duplex read-back later)
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

/* Read panel data over MISO (both buses): sends cmd, clocks nskip dummy
 * bytes, then reads nread bytes into out. */
void LCD_ReadBytes(uint8_t cmd, uint8_t *out, uint8_t nskip, uint8_t nread);

/* ---- bus selection (SOFT bit-bang vs HW SPI1) ---- */
void    LCD_UseSoftBus(void);   /* re-mux PA5/PA7 to GPIO, idle high      */
void    LCD_UseHwBus(void);     /* re-mux PA5/PA6/PA7 to SPI1 AF5 + init  */
uint8_t LCD_BusIsHw(void);      /* 1 while the HW SPI1 bus is selected    */
unsigned long LCD_HwSpiKHz(void); /* active SPI1 baud in kHz (info page)  */
void    SPI_HW_Flush(void);     /* drain the HW TX buffer (blocking)      */

#endif /* __INTERFACE_H */