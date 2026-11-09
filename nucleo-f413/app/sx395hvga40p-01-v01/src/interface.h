/*
  interface.h - low-level ILI9488 bus primitives (nucleo-f413 port).

  Standard 4-wire serial: CS frames each transfer, DC selects command vs
  data, the panel latches SDA on the rising edge. Two drive methods,
  selected at runtime:

    SOFT: PA5/PA7 bit-banged (idle-high SCL).
    HW  : SPI1 AF5 (PA5 = SCK, PA7 = MOSI), mode 3 (CPOL=1, CPHA=2EDGE -
          the same idle-high / rising-edge-sampling timing the bit-bang
          produces), 8-bit, MSB first, NSS soft. APB2 = 100 MHz natively
          on the F413 board tree: default prescaler /8 = 12.5 MHz SCK
          (debug); /4 = 25 MHz and /2 = 50 MHz via LCD_SPI1_PRESC. HW
          raster bursts stream through a 512-byte TX buffer.

    SCL = PA5, SDA = PA7 (soft GPIO or SPI1 AF5)
    DC  = PD15 (data/command, always GPIO)
    RES = PF12 (reset, always GPIO)
    CS  = PD14 (chip select, always GPIO)
    BL  = PE9 (TIM1_CH1 backlight PWM)
    MISO = PA6 (panel SDO, connected for HW read-back diagnostics).
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
void    LCD_UseHwBus(void);     /* re-mux PA5/PA6/PA7 to SPI1 AF5 + init */
uint8_t LCD_ReadBytes(uint8_t cmd, uint8_t *out, uint8_t nread);
                                /* HW-only read; 0=error, 1=success       */
uint8_t LCD_BusIsHw(void);      /* 1 while the HW SPI1 bus is selected    */
unsigned long LCD_HwSpiKHz(void); /* active SPI1 baud in kHz (info page)  */
void    LCD_HwSetPrescaler(uint8_t presc); /* runtime SCK switch (sweep) */
void    LCD_SoftSetPace(uint8_t iterations); /* per-bit delay (sweep)   */
void    LCD_SoftSetSlew(uint8_t high);   /* GPIO slew LOW/HIGH (sweep)  */
void    SPI_HW_Flush(void);     /* drain the HW TX buffer (blocking)      */

#endif /* __INTERFACE_H */