/*
  interface.h - FSMC bus primitives for the JD9858 panel (nano-f407).

  The module is wired like the vendor's F103VET6 example: NE1 = CS,
  A16 = DC, NOE = RD, NWE = WR, D0..D7 (8-bit). Writes go straight to
  the FSMC bank addresses - one store = one panel transaction:

    command (DC low): 0x6000_0000
    data    (DC high): 0x6001_0000

  Controller: NE1, NOR type, 8-bit, access mode B, ADDSET = 2,
  DATAST = 5 (vendor HAL settings, HAL_SRAM_Init).
*/

#ifndef __INTERFACE_H
#define __INTERFACE_H

#include <stdint.h>

void WriteComm(uint16_t data);
void WriteData(uint16_t data);
void SendData(uint32_t color);
void LCD_WriteDataFast(uint8_t data);   /* raw byte into the data window */
void LCD_FillBulk(uint32_t color, uint32_t pixels); /* solid burst */
void LCD_BeginData(void);                /* no-op (FSMC-decoded CS/DC)   */
void LCD_EndData(void);                  /* no-op (FSMC-decoded CS/DC)   */

/* ---- FSMC bus ---- */
void LCD_UseHwBus(void);        /* GPIO + FSMC controller bring-up */
unsigned long LCD_FsmcKHz(void); /* effective write rate, kHz (info page) */
void    LCD_FsmcSetTiming(uint8_t datast); /* runtime DATAST ramp */

#endif /* __INTERFACE_H */
