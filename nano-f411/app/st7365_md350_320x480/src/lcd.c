/*
  lcd.c - ST7365P 3.5" 320x480 LCD driver (nano-f411 port, MD350 module).
  ST7365P (ST7796-register-compatible) init sequence and the standard
  4-wire serial drawing API:
    - MADCTL 0x48 (portrait), 16bpp COLMOD,
    - panel power/VCOM/gamma init (0xF0 command-set enable, 0xE8 display
      output control, 0xC5 VCOM, 0xC2, 0xE0/0xE1 gamma),
    - 320x480 windows at COL_Pre = 0, ROW_Pre = 0.
  Wiring: SCL=PA5, SDA=PA7, RES=PA3, DC=PA4, CS=PB8, BL=PB9 (TIM4_CH4
  PWM). MISO=PA6 exists on the module but is not used by the write-only
  soft driver (for a future HW SPI1 path it maps to SPI1_MISO at AF5).
*/

#include <string.h>
#include "board.h"
#include "lcd.h"
#include "interface.h"
#include "blockwrite.h"

/* =====================================================================
   GPIO setup: PA4(DC) PA5(SCL) PA7(SDA) PA3(RES) PB8(CS) PB9(BL, via
   backlight.c as TIM4_CH4 PWM). MISO = PA6 is not driven by this
   write-only soft driver.
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
    HAL_Delay(20);
    LCD_RST_CLR;
    HAL_Delay(20);
    LCD_RST_SET;
    HAL_Delay(200);
}

/* =====================================================================
   Init sequence - ST7365P (ST7796-register-compatible): command-set
   enable (0xF0), MADCTL 0x48 (portrait), 16bpp COLMOD, display output
   control (0xE8), VCOM (0xC5), power (0xC2), positive/negative gamma
   (0xE0/0xE1), command-set close, display on.
   ===================================================================== */
void LCD_IC_Init(void)
{
    HAL_Delay(100);

    WriteComm(0x11);              /* Sleep out */
    HAL_Delay(100);

    WriteComm(0xF0);              /* Command set enable, part 1 */
    WriteData(0xC3);
    WriteComm(0xF0);
    WriteData(0x96);

    WriteComm(0x36);              /* MADCTL: portrait, BGR */
    WriteData(0x48);

    WriteComm(0x3A);              /* COLMOD 16bpp */
    WriteData(0x05);

    WriteComm(0xE8);              /* Display output control */
    WriteData(0x40); WriteData(0x82); WriteData(0x07);
    WriteData(0x18); WriteData(0x27); WriteData(0x0A);
    WriteData(0xB6); WriteData(0x33);

    WriteComm(0xC5);              /* VCOM */
    WriteData(0x27);

    WriteComm(0xC2);              /* Power control */
    WriteData(0xA7);

    WriteComm(0xE0);              /* Positive gamma */
    WriteData(0xF0); WriteData(0x01); WriteData(0x06); WriteData(0x0F);
    WriteData(0x12); WriteData(0x1D); WriteData(0x36); WriteData(0x54);
    WriteData(0x44); WriteData(0x0C); WriteData(0x18); WriteData(0x16);
    WriteData(0x13); WriteData(0x15);

    WriteComm(0xE1);              /* Negative gamma */
    WriteData(0xF0); WriteData(0x01); WriteData(0x05); WriteData(0x0A);
    WriteData(0x0B); WriteData(0x07); WriteData(0x32); WriteData(0x44);
    WriteData(0x44); WriteData(0x0C); WriteData(0x18); WriteData(0x17);
    WriteData(0x13); WriteData(0x16);

    WriteComm(0xF0);              /* Command set enable, part 2 */
    WriteData(0x3C);
    WriteComm(0xF0);
    WriteData(0x69);

    HAL_Delay(20);
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

/* Reset + full re-init sequence: the panel's controller is reset so the
 * init sequence applies cleanly. */
void LCD_Reinit(void)
{
    LCD_RESET();
    LCD_IC_Init();
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
   Vendor demo screens (lcd.c) - identical geometry/ordering. The pixel
   loops stream bytes inside one BeginData/EndData burst (no per-pixel
   CS/DC toggling, HW bursts batch into 512-byte chunks).
   ===================================================================== */
void DispColor(uint32_t color)
{
    int i, j;
    BlockWrite(COL_Pre, COL + COL_Pre - 1, ROW_Pre, ROW + ROW_Pre - 1);
    LCD_BeginData();
    for (i = 0; i < ROW; i++)
    {
        for (j = 0; j < COL; j++)
        {
            LCD_WriteDataFast((uint8_t)(color >> 8));
            LCD_WriteDataFast((uint8_t)color);
        }
    }
    LCD_EndData();
}

void DispFrame(void)
{
    int i, j;
    BlockWrite(COL_Pre, COL + COL_Pre - 1, ROW_Pre, ROW + ROW_Pre - 1);
    LCD_BeginData();
    LCD_WriteDataFast(0xF8); LCD_WriteDataFast(0x00);
    for (i = 0; i < COL - 2; i++) { LCD_WriteDataFast(0xFF); LCD_WriteDataFast(0xFF); }
    LCD_WriteDataFast(0x00); LCD_WriteDataFast(0x1F);
    for (j = 0; j < ROW - 2; j++)
    {
        LCD_WriteDataFast(0xF8); LCD_WriteDataFast(0x00);
        for (i = 0; i < COL - 2; i++) { LCD_WriteDataFast(0x00); LCD_WriteDataFast(0x00); }
        LCD_WriteDataFast(0x00); LCD_WriteDataFast(0x1F);
    }
    LCD_WriteDataFast(0xF8); LCD_WriteDataFast(0x00);
    for (i = 0; i < COL - 2; i++) { LCD_WriteDataFast(0xFF); LCD_WriteDataFast(0xFF); }
    LCD_WriteDataFast(0x00); LCD_WriteDataFast(0x1F);
    LCD_EndData();
}

void DispGrayHor16(void)
{
    int i, j, k;
    BlockWrite(COL_Pre, COL + COL_Pre - 1, ROW_Pre, ROW + ROW_Pre - 1);
    LCD_BeginData();
    for (i = 0; i < ROW; i++)
    {
        for (j = 0; j < COL % 16; j++) { LCD_WriteDataFast(0); LCD_WriteDataFast(0); }
        for (j = 0; j < 16; j++)
        {
            uint16_t c = (uint16_t)(((((j * 2) << 3) | ((j * 4) >> 3)) << 8) |
                                    (((j * 4) << 5) | (j * 2)));
            for (k = 0; k < COL / 16; k++)
            {
                LCD_WriteDataFast((uint8_t)(c >> 8));
                LCD_WriteDataFast((uint8_t)c);
            }
        }
    }
    LCD_EndData();
}

void DispBand(void)
{
    static const uint16_t color[8] = { 0xF800, 0xF800, 0x07E0, 0x07E0,
                                       0x001F, 0x001F, 0xFFFF, 0xFFFF };
    int i, j, k;
    BlockWrite(COL_Pre, COL + COL_Pre - 1, ROW_Pre, ROW + ROW_Pre - 1);
    LCD_BeginData();
    for (i = 0; i < 8; i++)
    {
        for (j = 0; j < ROW / 8; j++)
        {
            for (k = 0; k < COL; k++)
            {
                LCD_WriteDataFast((uint8_t)(color[i] >> 8));
                LCD_WriteDataFast((uint8_t)color[i]);
            }
        }
    }
    for (j = 0; j < ROW % 8; j++)
    {
        for (k = 0; k < COL; k++)
        {
            LCD_WriteDataFast((uint8_t)(color[7] >> 8));
            LCD_WriteDataFast((uint8_t)color[7]);
        }
    }
    LCD_EndData();
}

void StopDelay(uint16_t ms)
{
    HAL_Delay(ms);
}

/* =====================================================================
   Colors, addressing, text and 2D drawing - the standard st7789-style
   drawing API, adapted to the 320x480 ST7365P.
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
    uint16_t index;
    uint8_t  disChar;
    /* Must hold the LARGEST font's pixels (8x16 = 128); the 6x12 font
     * only fills the first 72. */
    uint16_t Buff[8 * 16];

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
    /* Row-major fill matching the font layout: each glyph row is
     * bytesPerRow bytes, bit 0 = leftmost pixel (the same convention the
     * transparent path uses). Filling linearly from the raw bit stream
     * instead smears the 6x12 font: its 6-bit rows are byte-packed with 2
     * padding bits that would bleed into the next row. */
    uint16_t bytesPerRow = s_AsciiFont->Sizes / s_AsciiFont->Height;
    for (uint16_t row = 0; row < s_AsciiFont->Height; row++)
    {
        for (uint16_t col = 0; col < s_AsciiFont->Width; col++)
        {
            disChar = s_AsciiFont->pTable[(uint16_t)c * s_AsciiFont->Sizes
                      + (uint16_t)row * bytesPerRow + (col / 8)];
            Buff[index++] = (disChar & (uint8_t)(1U << (col % 8)))
                            ? s_Color : s_BackColor;
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