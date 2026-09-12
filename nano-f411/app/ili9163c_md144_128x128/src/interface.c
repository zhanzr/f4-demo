/*
  interface.c - low-level ILI9163C bus primitives (nano-f411 port, soft SPI).

  3-wire serial (the module connector has no D/C pin): every byte is a
  9-bit frame - the D/C bit (0 = command, 1 = data) is clocked first, then
  the 8 data bits, MSB first. Both bits use the same edge convention: the
  line is set while SCL is high, then SCL falls and rises (panel latches
  on the rising edge).

    WriteComm(): CS low, D/C=0 frame, CS high
    WriteData(): CS low, D/C=1 frame, CS high
    LCD_BeginData()/LCD_WriteDataFast()/LCD_EndData():
        raster bursts hold CS low and emit one D/C=1 + 8-bit frame per
        byte (no per-byte CS toggling)
    SendData(): one 16-bit color as two D/C=1 frames, CS low (the vendor
        screen bodies frame CS around whole fills)

    SCL = PA5, SDA = PA7 (bit-banged), RES = PA6, CS = PB8 (GPIO).
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

/* Clock out one bit (set while SCL high, latch on the rising edge). */
static void SendBit(uint8_t v)
{
    if (v != 0U)
    {
        LCD_SPI_SDA_SET;
    }
    else
    {
        LCD_SPI_SDA_CLR;
    }
    LCD_SPI_SCL_CLR;
    LCD_SPI_SCL_SET;
}

/* Shift out one 8-bit byte, MSB first. */
static void SendByte(uint8_t dat)
{
    for (int i = 0; i < 8; i++)
    {
        SendBit((dat & 0x80U) != 0U);
        dat <<= 1;
    }
}

/* Write a register/command: 9-bit frame with D/C = 0. */
void WriteComm(uint16_t data)
{
    LCD_CS_CLR;
    SendBit(0U);
    SendByte((uint8_t)data);
    LCD_CS_SET;
}

/* Write a data byte: 9-bit frame with D/C = 1. */
void WriteData(uint16_t data)
{
    LCD_CS_CLR;
    SendBit(1U);
    SendByte((uint8_t)data);
    LCD_CS_SET;
}

/* One 16-bit color as two D/C=1 frames, no CS toggling (the callers -
 * the TEST_STAND fill bodies - hold CS low around whole fills). */
void SendData(uint32_t color)
{
    SendBit(1U);
    SendByte((uint8_t)(color >> 8));
    SendBit(1U);
    SendByte((uint8_t)color);
}

/* Fast raw 8-bit data byte as a D/C=1 frame: caller manages CS
 * (LCD_BeginData/LCD_EndData frame the burst). */
void LCD_WriteDataFast(uint8_t data)
{
    SendBit(1U);
    SendByte(data);
}

/* Begin/end a raster burst: CS held low across the bytes. */
void LCD_BeginData(void)
{
    LCD_CS_CLR;
}
void LCD_EndData(void)
{
    LCD_CS_SET;
}