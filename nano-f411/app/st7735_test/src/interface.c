/*
  interface.c - low-level ST7735S bus primitives (nano-f411 port).
  Ported from the vendor example C8T6_md144_t1 (HARDWARE/interface/interface.c,
  LCD_SPI4Line + commdata16 unset): the clock/data are bit-banged GPIO, with
  DC selecting command vs data and CS asserted per transfer.
    SCL = PA5, SDA = PA6, DC = PA4, RES = PA7, CS = PB8
*/

#include "interface.h"
#include "lcd.h"

void CS_SET(void)
{
    LCD_CS_SET;
}
void CS_CLR(void)
{
    LCD_CS_CLR;
}

/* Shift out one 8-bit byte, MSB first, on the SCL/SDA lines. */
static void SendDataSPI(uint8_t dat)
{
    for (int i = 0; i < 8; i++)
    {
        if ((dat & 0x80U) != 0U)
        {
            LCD_SPI_SDA_SET;
        }
        else
        {
            LCD_SPI_SDA_CLR;
        }
        dat <<= 1;
        LCD_SPI_SCL_CLR;
        LCD_SPI_SCL_SET;
    }
}

/* Write a register/command: DC low. */
void WriteComm(uint16_t data)
{
    LCD_CS_CLR;
    LCD_RS_CLR;
    SendDataSPI((uint8_t)data);
    LCD_CS_SET;
}

/* Write a data byte: DC high. */
void WriteData(uint16_t data)
{
    LCD_CS_CLR;
    LCD_RS_SET;
    SendDataSPI((uint8_t)data);
    LCD_CS_SET;
}

/* Stream one RGB565 pixel color as two 8-bit writes (DC high persists). */
void SendData(uint32_t color)
{
    LCD_CS_CLR;
    LCD_RS_SET;
    SendDataSPI((uint8_t)(color >> 8));
    SendDataSPI((uint8_t)color);
    LCD_CS_SET;
}

/* Fast raw 8-bit data byte: no CS/DC toggling (caller manages both). */
void LCD_WriteDataFast(uint8_t data)
{
    SendDataSPI(data);
}

/* Begin/end a data-gram burst for raster fills: DC high, CS held low. */
void LCD_BeginData(void)
{
    LCD_CS_CLR;
    LCD_RS_SET;
}
void LCD_EndData(void)
{
    LCD_CS_SET;
}