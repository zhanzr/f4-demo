/*
  lcd.c - ST7735S 1.44" 128x128 LCD driver (nano-f411 port).
  Ported from the vendor example C8T6_md144_t1 (HARDWARE/lcd/lcd.c), trimmed
  to the 4-wire SPI interface and the demo screens used by this project:
  DispFrame / DispGrayHor16 / DispBand / DispColor, mirroring the vendor's
  TEST_STAND loop. Init sequence from 001_006_ST7735S_1.44_0xC8.h.
*/

#include <string.h>
#include "board.h"
#include "lcd.h"
#include "interface.h"
#include "blockwrite.h"

/* =====================================================================
   GPIO setup: PA4(DC) PA5(SCL) PA6(SDA) PA7(RES) PB8(CS) PB9(BL, via
   backlight.c as TIM4_CH4 PWM). Mirrors the vendor's LCD_GPIOInit for the
   pins this interface actually uses.
   ===================================================================== */
void LCD_GPIOInit(void)
{
    GPIO_InitTypeDef g;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;

    g.Pin = LCD_SCL_Pin;  HAL_GPIO_Init(LCD_GPIO_PortSCL, &g);
    g.Pin = LCD_SDA_Pin;  HAL_GPIO_Init(LCD_GPIO_PortSDA, &g);
    g.Pin = LCD_RS_Pin;   HAL_GPIO_Init(LCD_GPIO_PortRS,  &g);
    g.Pin = LCD_RST_Pin;  HAL_GPIO_Init(LCD_GPIO_PortRST, &g);
    g.Pin = LCD_CS_Pin;   HAL_GPIO_Init(LCD_GPIO_PortCS,  &g);

    /* Idle levels: SCL high, DC high (data), RES high, CS high (deselected). */
    LCD_SPI_SCL_SET;
    LCD_RS_SET;
    LCD_RST_SET;
    LCD_CS_SET;
}

void LCD_RESET(void)
{
    LCD_RST_SET;
    HAL_Delay(50);
    LCD_RST_CLR;
    HAL_Delay(200);
    LCD_RST_SET;
    HAL_Delay(200);
}

/* =====================================================================
   Init sequence - 001_006_ST7735S_1.44_0xC8.h (vendor, verbatim commands)
   ===================================================================== */
void LCD_IC_Init(void)
{
    WriteComm(0x11);              /* Sleep out */
    HAL_Delay(120);

    /* Frame rate */
    WriteComm(0xB1);
    WriteData(0x05); WriteData(0x3A); WriteData(0x3A);
    WriteComm(0xB2);
    WriteData(0x05); WriteData(0x3A); WriteData(0x3A);
    WriteComm(0xB3);
    WriteData(0x05); WriteData(0x3A); WriteData(0x3A);
    WriteData(0x05); WriteData(0x3A); WriteData(0x3A);

    WriteComm(0xB4);              /* Dot inversion */
    WriteData(0x03);

    /* Power sequence */
    WriteComm(0xC0);
    WriteData(0x62); WriteData(0x02); WriteData(0x04);
    WriteComm(0xC1);
    WriteData(0xC0);
    WriteComm(0xC2);
    WriteData(0x0D); WriteData(0x00);
    WriteComm(0xC3);
    WriteData(0x8D); WriteData(0x6A);
    WriteComm(0xC4);
    WriteData(0x8D); WriteData(0xEE);

    WriteComm(0xC5);              /* VCOM */
    WriteData(0x12);

    /* Gamma */
    WriteComm(0xE0);
    WriteData(0x03); WriteData(0x1B); WriteData(0x12); WriteData(0x11);
    WriteData(0x3F); WriteData(0x3A); WriteData(0x32); WriteData(0x34);
    WriteData(0x2F); WriteData(0x2B); WriteData(0x30); WriteData(0x3A);
    WriteData(0x00); WriteData(0x01); WriteData(0x02); WriteData(0x05);
    WriteComm(0xE1);
    WriteData(0x03); WriteData(0x1B); WriteData(0x12); WriteData(0x11);
    WriteData(0x32); WriteData(0x2F); WriteData(0x2A); WriteData(0x2F);
    WriteData(0x2E); WriteData(0x2C); WriteData(0x35); WriteData(0x3F);
    WriteData(0x00); WriteData(0x00); WriteData(0x01); WriteData(0x05);

    WriteComm(0xFC);              /* Enable gate power save mode */
    WriteData(0x8C);
    WriteComm(0x3A);              /* 65k mode */
    WriteData(0x05);
    WriteComm(0x36);              /* MADCTL 0xC8: 1.44", BGR? per vendor */
    WriteData(0xC8);
    WriteComm(0x29);              /* Display on */
}

void LCD_Init(void)
{
    LCD_GPIOInit();
    LCD_RESET();
    LCD_IC_Init();

    /* Clear to black. */
    DispColor(BLACK);
}

/* =====================================================================
   BlockWrite - blockwrite_default.h (vendor, verbatim): set pixel window
   then leave CS/DC ready for the raster dump.
   ===================================================================== */
void BlockWrite(uint16_t Xstart, uint16_t Xend, uint16_t Ystart, uint16_t Yend)
{
    WriteComm(0x2A);
    WriteData((uint16_t)(Xstart >> 8));
    WriteData((uint16_t)Xstart);
    WriteData((uint16_t)(Xend >> 8));
    WriteData(Xend);

    WriteComm(0x2B);
    WriteData((uint16_t)(Ystart >> 8));
    WriteData((uint16_t)Ystart);
    WriteData((uint16_t)(Yend >> 8));
    WriteData(Yend);

    WriteComm(0x2C);
}

/* =====================================================================
   Vendor demo screens (lcd.c) - identical geometry/ordering.
   ===================================================================== */
void DispColor(uint32_t color)
{
    int i, j;
    BlockWrite(COL_Pre, COL + COL_Pre - 1, ROW_Pre, ROW + ROW_Pre - 1);
    LCD_CS_CLR;
    LCD_RS_SET;
    for (i = 0; i < ROW; i++)
    {
        for (j = 0; j < COL; j++)
        {
            SendData(color);
        }
    }
    LCD_CS_SET;
}

void DispFrame(void)
{
    int i, j;
    BlockWrite(COL_Pre, COL + COL_Pre - 1, ROW_Pre, ROW + ROW_Pre - 1);
    LCD_CS_CLR;
    LCD_RS_SET;
    SendData(0xF800);
    for (i = 0; i < COL - 2; i++) { SendData(0xFFFF); }
    SendData(0x001F);
    for (j = 0; j < ROW - 2; j++)
    {
        SendData(0xF800);
        for (i = 0; i < COL - 2; i++) { SendData(0x0000); }
        SendData(0x001F);
    }
    SendData(0xF800);
    for (i = 0; i < COL - 2; i++) { SendData(0xFFFF); }
    SendData(0x001F);
    LCD_CS_SET;
}

void DispGrayHor16(void)
{
    int i, j, k;
    BlockWrite(COL_Pre, COL + COL_Pre - 1, ROW_Pre, ROW + ROW_Pre - 1);
    LCD_CS_CLR;
    LCD_RS_SET;
    for (i = 0; i < ROW; i++)
    {
        for (j = 0; j < COL % 16; j++) { SendData(0); }
        for (j = 0; j < 16; j++)
        {
            for (k = 0; k < COL / 16; k++)
            {
                SendData((((((j * 2) << 3) | ((j * 4) >> 3)) << 8) |
                          (((j * 4) << 5) | (j * 2))));
            }
        }
    }
    LCD_CS_SET;
}

void DispBand(void)
{
    static const uint32_t color[8] = { 0xF800, 0xF800, 0x07E0, 0x07E0,
                                       0x001F, 0x001F, 0xFFFF, 0xFFFF };
    int i, j, k;
    BlockWrite(COL_Pre, COL + COL_Pre - 1, ROW_Pre, ROW + ROW_Pre - 1);
    LCD_CS_CLR;
    LCD_RS_SET;
    for (i = 0; i < 8; i++)
    {
        for (j = 0; j < ROW / 8; j++)
        {
            for (k = 0; k < COL; k++)
            {
                SendData(color[i]);
            }
        }
    }
    for (j = 0; j < ROW % 8; j++)
    {
        for (k = 0; k < COL; k++)
        {
            SendData(color[7]);
        }
    }
    LCD_CS_SET;
}

void StopDelay(uint16_t ms)
{
    HAL_Delay(ms);
}