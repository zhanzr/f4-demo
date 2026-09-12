/*
  interface.h - low-level NV3030B bus primitives (nano-f411 port).

  The NV3030B on this module runs its QSPI-compatible single-lane serial
  protocol (the vendor example drives it exactly this way, and its
  "LCD_DC" pin is never used - command/data framing is carried by the
  wrapped transaction format instead). Two drive methods, selected at
  runtime:

    SOFT: PA5/PA7 bit-banged (idle-high SCL, latch on the rising edge).
    HW  : SPI1 AF5 (PA5 = SCK, PA7 = MOSI), mode 3, 8-bit, MSB first,
          NSS soft. APB2 = 100 MHz (project clock override): default
          prescaler /2 = 50 MHz SCK (the F411 SPI1 max); /4 = 25 MHz
          available via LCD_SPI1_PRESC. HW raster bursts stream through
          a 512-byte TX buffer.

  Transaction framing (vendor-verbatim):
    WriteComm(cmd): CS high, CS low, send 02 00 cmd 00, CS STAYS LOW -
                    the frame stays open for the parameters/data that
                    follow.
    WriteData(b) / SendData / LCD_WriteDataFast: stream bytes into the
                    open frame.
    WriteComm's leading CS high closes the previous frame; LCD_EndData
    (raster bursts) raises CS as well.

    SCL = PA5, SDA/MOSI = PA7, CS = PA4 (GPIO).
    DC  = PA6  - NOT USED (vendor defines it but never drives it; the
                 wrapped format carries command/data framing).
    MISO - the module does not expose a usable MISO for this strap; the
    panel is write-only through this interface.
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

/* ---- bus selection (SOFT bit-bang vs HW SPI1) ---- */
void    LCD_UseSoftBus(void);   /* re-mux PA5/PA7 to GPIO, idle high      */
void    LCD_UseHwBus(void);     /* re-mux PA5/PA7 to SPI1 AF5 + init      */
uint8_t LCD_BusIsHw(void);      /* 1 while the HW SPI1 bus is selected    */
unsigned long LCD_HwSpiKHz(void); /* active SPI1 baud in kHz (info page)  */
void    SPI_HW_Flush(void);     /* drain the HW TX buffer (blocking)      */

#endif /* __INTERFACE_H */