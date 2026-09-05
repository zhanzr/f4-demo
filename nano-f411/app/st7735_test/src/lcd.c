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

/* =====================================================================
   Colors, addressing, text and 2D drawing - ported from the h723-mini
   st7789 example (lcd_spi_154.c), adapted to the 128x128 ST7735S.
   ===================================================================== */

/* Current foreground/background RGB565 + text-transparent flag. */
static uint16_t s_Color     = BLACK;
static uint16_t s_BackColor = BLACK;
static uint8_t  s_Transparent = 0;
static pFONT  *s_AsciiFont = NULL;

/* Convert a 24-bit RGB888 into a 16-bit RGB565 word. */
static uint16_t rgb888_to_rgb565(uint32_t c)
{
    uint16_t r = (uint16_t)((c & 0x00F80000UL) >> 8);
    uint16_t g = (uint16_t)((c & 0x0000FC00UL) >> 5);
    uint16_t b = (uint16_t)((c & 0x000000F8UL) >> 3);
    return (uint16_t)(r | g | b);
}

void LCD_SetColor(uint32_t rgb888)
{
    s_Color = rgb888_to_rgb565(rgb888);
}

void LCD_SetBackColor(uint32_t rgb888)
{
    s_BackColor = rgb888_to_rgb565(rgb888);
}

/* Set the pixel window using the panel's column/row pre-offsets (128x128,
   Y offset 32 for the 1.44" 0xC8 orientation), then leave CS/DC for data. */
void LCD_SetAddress(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    WriteComm(0x2A);
    WriteData((uint16_t)((x1 + COL_Pre) >> 8));
    WriteData((uint16_t)(x1 + COL_Pre));
    WriteData((uint16_t)((x2 + COL_Pre) >> 8));
    WriteData((uint16_t)(x2 + COL_Pre));

    WriteComm(0x2B);
    WriteData((uint16_t)((y1 + ROW_Pre) >> 8));
    WriteData((uint16_t)(y1 + ROW_Pre));
    WriteData((uint16_t)((y2 + ROW_Pre) >> 8));
    WriteData((uint16_t)(y2 + ROW_Pre));

    WriteComm(0x2C);
}

void LCD_Clear(void)
{
    uint32_t n = (uint32_t)COL * ROW;
    LCD_SetAddress(0, 0, COL - 1, ROW - 1);
    LCD_BeginData();
    while (n--)
    {
        LCD_WriteDataFast((uint8_t)(s_BackColor >> 8));
        LCD_WriteDataFast((uint8_t)s_BackColor);
    }
    LCD_EndData();
}

void LCD_ClearRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    uint32_t n = (uint32_t)width * height;
    if (n == 0U) { return; }
    LCD_SetAddress(x, y, x + width - 1, y + height - 1);
    LCD_BeginData();
    while (n--)
    {
        LCD_WriteDataFast((uint8_t)(s_BackColor >> 8));
        LCD_WriteDataFast((uint8_t)s_BackColor);
    }
    LCD_EndData();
}

/* ---- ASCII text ---- */
void LCD_SetAsciiFont(pFONT *font)
{
    s_AsciiFont = font;
}

void LCD_ShowTransparent(uint8_t mode)
{
    s_Transparent = mode;
}

void LCD_DisplayChar(uint16_t x, uint16_t y, uint8_t c)
{
    uint16_t i, index;
    uint8_t  disChar;
    uint16_t Buff[6 * 12];

    if ((s_AsciiFont == NULL) || (c < 0x20U) || (c > 0x7EU))
    {
        return;
    }
    c -= 0x20U;   /* table starts at space */

    if (s_Transparent)
    {
        uint16_t bytesPerRow = s_AsciiFont->Sizes / s_AsciiFont->Height;
        for (uint16_t row = 0; row < s_AsciiFont->Height; row++)
        {
            for (uint16_t col = 0; col < s_AsciiFont->Width; col++)
            {
                disChar = s_AsciiFont->pTable[(uint16_t)c * s_AsciiFont->Sizes
                          + (uint16_t)row * bytesPerRow + (col / 8)];
                if (disChar & (uint8_t)(1U << (col % 8)))
                {
                    LCD_DrawPoint(x + col, y + row, s_Color);
                }
            }
        }
        return;
    }

    index = 0;
    for (i = 0; i < s_AsciiFont->Sizes; i++)
    {
        disChar = s_AsciiFont->pTable[(uint16_t)c * s_AsciiFont->Sizes + i];
        for (uint16_t bit = 0; bit < 8; bit++)
        {
            Buff[index++] = (disChar & (uint8_t)(1U << bit))
                            ? s_Color : s_BackColor;
            if (index >= s_AsciiFont->Width * s_AsciiFont->Height)
            {
                break;
            }
        }
    }
    LCD_CopyBuffer(x, y, s_AsciiFont->Width, s_AsciiFont->Height, Buff);
}

void LCD_DisplayString(uint16_t x, uint16_t y, char *p)
{
    while (x < COL && *p != '\0')
    {
        LCD_DisplayChar(x, y, (uint8_t)*p);
        x += (uint16_t)((s_AsciiFont != NULL) ? s_AsciiFont->Width : 6);
        p++;
    }
}

/* ---- 2D drawing ---- */
void LCD_DrawPoint(uint16_t x, uint16_t y, uint32_t color)
{
    LCD_SetAddress(x, y, x, y);
    LCD_BeginData();
    LCD_WriteDataFast((uint8_t)(color >> 8));
    LCD_WriteDataFast((uint8_t)color);
    LCD_EndData();
}

void LCD_DrawLine_V(uint16_t x, uint16_t y, uint16_t height)
{
    LCD_SetAddress(x, y, x, (uint16_t)(y + height - 1));
    LCD_BeginData();
    while (height--)
    {
        LCD_WriteDataFast((uint8_t)(s_Color >> 8));
        LCD_WriteDataFast((uint8_t)s_Color);
    }
    LCD_EndData();
}

void LCD_DrawLine_H(uint16_t x, uint16_t y, uint16_t width)
{
    LCD_SetAddress(x, y, (uint16_t)(x + width - 1), y);
    LCD_BeginData();
    while (width--)
    {
        LCD_WriteDataFast((uint8_t)(s_Color >> 8));
        LCD_WriteDataFast((uint8_t)s_Color);
    }
    LCD_EndData();
}

void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    int16_t deltax = 0, deltay = 0, x = 0, y = 0, xinc1 = 0, xinc2 = 0;
    int16_t yinc1 = 0, yinc2 = 0, den = 0, num = 0, numadd = 0;
    int16_t numpixels = 0, curpixel = 0;

    deltax = ABS((int16_t)x2 - (int16_t)x1);
    deltay = ABS((int16_t)y2 - (int16_t)y1);
    x = x1; y = y1;

    if (x2 >= x1) { xinc1 = 1; xinc2 = 1; } else { xinc1 = -1; xinc2 = -1; }
    if (y2 >= y1) { yinc1 = 1; yinc2 = 1; } else { yinc1 = -1; yinc2 = -1; }

    if (deltax >= deltay)
    {
        xinc1 = 0; yinc2 = 0; den = deltax; num = deltax / 2;
        numadd = deltay; numpixels = deltax;
    }
    else
    {
        xinc2 = 0; yinc1 = 0; den = deltay; num = deltay / 2;
        numadd = deltax; numpixels = deltay;
    }
    for (curpixel = 0; curpixel <= numpixels; curpixel++)
    {
        LCD_DrawPoint((uint16_t)x, (uint16_t)y, s_Color);
        num += numadd;
        if (num >= den)
        {
            num -= den;
            x += xinc1;
            y += yinc1;
        }
        x += xinc2;
        y += yinc2;
    }
}

void LCD_DrawRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    LCD_DrawLine_H(x, y, width);
    LCD_DrawLine_H(x, (uint16_t)(y + height - 1), width);
    LCD_DrawLine_V(x, y, height);
    LCD_DrawLine_V((uint16_t)(x + width - 1), y, height);
}

void LCD_DrawCircle(uint16_t x, uint16_t y, uint16_t r)
{
    int16_t Xadd = -(int16_t)r, Yadd = 0, err = 2 - 2 * (int16_t)r, e2;
    do
    {
        LCD_DrawPoint((uint16_t)(x - Xadd), (uint16_t)(y + Yadd), s_Color);
        LCD_DrawPoint((uint16_t)(x + Xadd), (uint16_t)(y + Yadd), s_Color);
        LCD_DrawPoint((uint16_t)(x + Xadd), (uint16_t)(y - Yadd), s_Color);
        LCD_DrawPoint((uint16_t)(x - Xadd), (uint16_t)(y - Yadd), s_Color);
        e2 = err;
        if (e2 <= Yadd)
        {
            Yadd++;
            err += (int16_t)(Yadd * 2 + 1);
            if (-Xadd == Yadd && e2 <= Xadd) { e2 = 0; }
        }
        if (e2 > Xadd)
        {
            Xadd++;
            err += (int16_t)(Xadd * 2 + 1);
        }
    }
    while (Xadd <= 0);
}

void LCD_FillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    uint32_t n = (uint32_t)width * height;
    if (n == 0U) { return; }
    LCD_SetAddress(x, y, (uint16_t)(x + width - 1), (uint16_t)(y + height - 1));
    LCD_BeginData();
    while (n--)
    {
        LCD_WriteDataFast((uint8_t)(s_Color >> 8));
        LCD_WriteDataFast((uint8_t)s_Color);
    }
    LCD_EndData();
}

void LCD_FillCircle(uint16_t x, uint16_t y, uint16_t r)
{
    int32_t  D;
    uint32_t CurX, CurY;

    D = 3 - ((int32_t)r << 1);
    CurX = 0;
    CurY = r;
    while (CurX <= CurY)
    {
        if (CurY > 0)
        {
            LCD_DrawLine_V((uint16_t)(x - CurX), (uint16_t)(y - CurY),
                           (uint16_t)(2 * CurY));
            LCD_DrawLine_V((uint16_t)(x + CurX), (uint16_t)(y - CurY),
                           (uint16_t)(2 * CurY));
        }
        if (CurX > 0)
        {
            LCD_DrawLine_V((uint16_t)(x - CurY), (uint16_t)(y - CurX),
                           (uint16_t)(2 * CurX));
            LCD_DrawLine_V((uint16_t)(x + CurY), (uint16_t)(y - CurX),
                           (uint16_t)(2 * CurX));
        }
        if (D < 0)
        {
            D += (int32_t)(CurX << 2) + 6;
        }
        else
        {
            D += (int32_t)((CurX - CurY) << 2) + 10;
            CurY--;
        }
        CurX++;
    }
    LCD_DrawCircle(x, y, r);
}

void LCD_CopyBuffer(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                    uint16_t *data)
{
    uint32_t n = (uint32_t)width * height;
    LCD_SetAddress(x, y, (uint16_t)(x + width - 1), (uint16_t)(y + height - 1));
    LCD_BeginData();
    while (n--)
    {
        LCD_WriteDataFast((uint8_t)(*data >> 8));
        LCD_WriteDataFast((uint8_t)*data);
        data++;
    }
    LCD_EndData();
}