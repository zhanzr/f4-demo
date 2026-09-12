/*
  interface.c - low-level ST7365P bus primitives (nano-f411 port, soft SPI).

  Standard 4-wire serial, bit-banged: DC selects command vs data, CS
  asserts per transfer (and stays low across raster bursts), SCL idles
  high and the panel latches SDA on the rising edge.

    SCL = PA5, SDA = PA7, DC = PA4, RES = PA3, CS = PB8 (GPIO).
    MISO = PA6 is present on the module but not used by this write-only
    driver.
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

/* Shift out one 8-bit byte, MSB first (set while SCL high, latch on the
 * rising edge). */
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