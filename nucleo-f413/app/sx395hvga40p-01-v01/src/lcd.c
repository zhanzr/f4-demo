/*
  lcd.c - ILI9488 3.5" 320x480 LCD driver (nucleo-f413 port,
  SX395HVGA40P-01-V01 module). Classic ILI9488 init (vendor-verbatim:
  F7/C0/C1/C5/B1/B4/B6/B7/BE/E9 power+timing, gamma tables, 16bpp COLMOD,
  MADCTL 0x08, sleep out + 120 ms, display on) over 4-wire SPI - the
  command byte goes out with DC low, every data byte with DC high.

  Wiring: SCL=PA5, SDA=PA7, DC=PD15, RST=PF12, CS=PD14, BL=PE9
  (TIM1_CH1 PWM).
  Vendor example: D:\module_db\sx395hvga40p-01-v01\vendor_ex (F103).
*/

#include <string.h>
#include "board.h"
#include "lcd.h"
#include "interface.h"
#include "blockwrite.h"

/* ---- runtime drawing window ----
 * Every draw is relative to this window (origin + size). It defaults to
 * the full panel and can be shrunk at runtime (e.g. to a centered
 * 256x224 NES window); the surrounding area keeps its previous content. */
static uint16_t s_win_x = 0, s_win_y = 0;
static uint16_t s_win_w = COL, s_win_h = ROW;

void LCD_SetWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    if (x >= COL || y >= ROW) { return; }
    if (w == 0U || h == 0U)   { return; }
    if (x + w > COL) { w = (uint16_t)(COL - x); }
    if (y + h > ROW) { h = (uint16_t)(ROW - y); }
    s_win_x = x;
    s_win_y = y;
    s_win_w = w;
    s_win_h = h;
}

void LCD_ResetWindow(void)
{
    s_win_x = 0U;
    s_win_y = 0U;
    s_win_w = COL;
    s_win_h = ROW;
}

uint16_t LCD_W(void)
{
    return s_win_w;
}

uint16_t LCD_H(void)
{
    return s_win_h;
}


/* =====================================================================
   GPIO setup: DC=PD15, SCL=PA5, SDA=PA7, RST=PF12, CS=PD14 (BL=PE9
   lives in backlight.c as TIM1_CH1 PWM).
   ===================================================================== */
void LCD_GPIOInit(void)
{
    GPIO_InitTypeDef g;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();

    /* Highest GPIO speed: cleanest edges for the bit-banged bus. */
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

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
    /* Vendor RESET order: high 50 ms, low 200 ms, high. */
    LCD_RST_SET;
    HAL_Delay(50);
    LCD_RST_CLR;
    HAL_Delay(200);
    LCD_RST_SET;
    HAL_Delay(120);
}

/* =====================================================================
   Init sequence - ILI9488 (vendored SX040HVGA40P-01-V01 example,
   verbatim registers): F7 unlock, power/VCOM/timing (C0/C1/C5/B1/B4/
   B6/B7/BE/E9), MADCTL 0x08, 16bpp COLMOD, positive/negative gamma
   (E0/E1), sleep out + 120 ms, display on.
   ===================================================================== */
void LCD_IC_Init(void)
{
    WriteComm(0xF7);              /* Unlock vendor registers */
    WriteData(0xA9);
    WriteData(0x51);
    WriteData(0x2C);
    WriteData(0x82);

    WriteComm(0xC0);              /* Power control 1 */
    WriteData(0x11);
    WriteData(0x09);

    WriteComm(0xC1);              /* Power control 2 */
    WriteData(0x41);

    WriteComm(0xC5);              /* VCOM */
    WriteData(0x00);
    WriteData(0x2D);
    WriteData(0x80);

    WriteComm(0xB1);              /* Frame rate */
    WriteData(0xB0);
    WriteData(0x11);

    WriteComm(0xB4);              /* Display inversion control */
    WriteData(0x02);

    WriteComm(0xB6);              /* Display function control */
    WriteData(0x02);
    WriteData(0x22);

    WriteComm(0xB7);              /* Entry mode */
    WriteData(0xC6);

    WriteComm(0xBE);              /* Power control 3 */
    WriteData(0x00);
    WriteData(0x04);

    WriteComm(0xE9);              /* SPD control */
    WriteData(0x00);

    WriteComm(0x36);              /* MADCTL */
    WriteData(0x08);

    WriteComm(0x3A);              /* COLMOD 18bpp - REQUIRED in serial
                                     mode (the F103 vendor example's
                                     0x55/16bpp value is only valid on
                                     the parallel bus; 2-byte pixels in
                                     serial mode garble everything) */
    WriteData(0x66);

    WriteComm(0xB0);              /* Interface mode control (working
                                     Arduino DIYables driver writes it) */
    WriteData(0x00);

    WriteComm(0xE0);              /* Positive gamma */
    WriteData(0x00); WriteData(0x04); WriteData(0x01); WriteData(0x00);
    WriteData(0x0F); WriteData(0x08); WriteData(0x3C); WriteData(0x78);
    WriteData(0x49); WriteData(0x07); WriteData(0x0E); WriteData(0x0B);
    WriteData(0x15); WriteData(0x17); WriteData(0x0F);

    WriteComm(0xE1);              /* Negative gamma */
    WriteData(0x00); WriteData(0x16); WriteData(0x1C); WriteData(0x03);
    WriteData(0x0F); WriteData(0x04); WriteData(0x37); WriteData(0x23);
    WriteData(0x4E); WriteData(0x04); WriteData(0x0F); WriteData(0x0E);
    WriteData(0x3B); WriteData(0x38); WriteData(0x0F);

    WriteComm(0x11);              /* Sleep out */
    HAL_Delay(120);

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
   BlockWrite - set a pixel window relative to the current drawing
   window, then leave CS/DC ready for the raster dump.
   ===================================================================== */
void BlockWrite(uint16_t Xstart, uint16_t Xend, uint16_t Ystart, uint16_t Yend)
{
    WriteComm(0x2A);
    WriteData((uint16_t)((Xstart + s_win_x) >> 8));
    WriteData((uint16_t)(Xstart + s_win_x));
    WriteData((uint16_t)((Xend + s_win_x) >> 8));
    WriteData((uint16_t)(Xend + s_win_x));

    WriteComm(0x2B);
    WriteData((uint16_t)((Ystart + s_win_y) >> 8));
    WriteData((uint16_t)(Ystart + s_win_y));
    WriteData((uint16_t)((Yend + s_win_y) >> 8));
    WriteData((uint16_t)(Yend + s_win_y));

    WriteComm(0x2C);
}

/* =====================================================================
   Vendor demo screens (lcd.c) - identical geometry/ordering. The pixel
   loops stream bytes inside one BeginData/EndData burst (no per-pixel
   CS/DC toggling, HW bursts batch into 512-byte chunks).
   ===================================================================== */
/* =====================================================================
   ILI9488 serial mode pixels are 18-bit RGB666 - 3 bytes per pixel
   (COLMOD 0x3A = 0x66). Convert the internal RGB565 word and emit
   R, G, B (top-bit-aligned, as in the working Arduino DIYables driver).
   ===================================================================== */
static void lcd_write_pixel565(uint16_t c565)
{
    LCD_WriteDataFast((uint8_t)(((c565 >> 8) & 0xF8U) | ((c565 >> 13) & 0x07U)));
    LCD_WriteDataFast((uint8_t)(((c565 >> 3) & 0xFCU) | ((c565 >> 9) & 0x03U)));
    LCD_WriteDataFast((uint8_t)((c565 << 3) & 0xF8U));
}

void DispColor(uint32_t color)
{
    int i, j;
    BlockWrite(0, LCD_W() - 1U, 0, LCD_H() - 1U);
    LCD_BeginData();
    for (i = 0; i < (int)LCD_H(); i++)
    {
        for (j = 0; j < (int)LCD_W(); j++)
        {
            lcd_write_pixel565(color);
        }
    }
    LCD_EndData();
}

void DispFrame(void)
{
    /* red frame, white interior (18-bit pixels via the 565 helper) */
    int i, j;
    BlockWrite(0, LCD_W() - 1U, 0, LCD_H() - 1U);
    LCD_BeginData();
    for (j = 0; j < (int)LCD_H(); j++)
    {
        for (i = 0; i < (int)LCD_W(); i++)
        {
            if (j == 0 || j == (int)LCD_H() - 1 ||
                i == 0 || i == (int)LCD_W() - 1)
            {
                lcd_write_pixel565(0xF800U);   /* red */
            }
            else
            {
                lcd_write_pixel565(0xFFFFU);   /* white */
            }
        }
    }
    LCD_EndData();
}

void DispGrayHor16(void)
{
    int i, j, k;
    BlockWrite(0, LCD_W() - 1U, 0, LCD_H() - 1U);
    LCD_BeginData();
    for (i = 0; i < (int)LCD_H(); i++)
    {
        for (j = 0; j < (int)LCD_W() % 16; j++) { lcd_write_pixel565(0x0000U); }
        for (j = 0; j < 16; j++)
        {
            uint16_t c = (uint16_t)(((((j * 2) << 3) | ((j * 4) >> 3)) << 8) |
                                    (((j * 4) << 5) | (j * 2)));
            for (k = 0; k < (int)LCD_W() / 16; k++)
            {
                lcd_write_pixel565(c);
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
    BlockWrite(0, LCD_W() - 1U, 0, LCD_H() - 1U);
    LCD_BeginData();
    for (i = 0; i < 8; i++)
    {
        for (j = 0; j < (int)LCD_H() / 8; j++)
        {
            for (k = 0; k < (int)LCD_W(); k++)
            {
                lcd_write_pixel565(color[i]);
            }
        }
    }
    for (j = 0; j < (int)LCD_H() % 8; j++)
    {
        for (k = 0; k < (int)LCD_W(); k++)
        {
            lcd_write_pixel565(color[7]);
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
   drawing API, adapted to the 320x480 ILI9488.
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

/* Set the pixel window using the runtime drawing window (origin + size),
 * then leave CS/DC for data. */
void LCD_SetAddress(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    x1 = (uint16_t)(x1 + s_win_x);
    y1 = (uint16_t)(y1 + s_win_y);
    x2 = (uint16_t)(x2 + s_win_x);
    y2 = (uint16_t)(y2 + s_win_y);

    WriteComm(0x2A);
    WriteData((uint16_t)(x1 >> 8));
    WriteData((uint16_t)x1);
    WriteData((uint16_t)(x2 >> 8));
    WriteData((uint16_t)x2);

    WriteComm(0x2B);
    WriteData((uint16_t)(y1 >> 8));
    WriteData((uint16_t)y1);
    WriteData((uint16_t)(y2 >> 8));
    WriteData((uint16_t)y2);

    WriteComm(0x2C);
}

void LCD_Clear(void)
{
    uint32_t n = (uint32_t)s_win_w * s_win_h;
    LCD_SetAddress(0, 0, s_win_w - 1, s_win_h - 1);
    LCD_BeginData();
    while (n--)
    {
        lcd_write_pixel565(s_BackColor);
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
        lcd_write_pixel565(s_BackColor);
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

/* Scale-up glyph renderer: draws the current font's glyph with every
 * pixel expanded to scale x scale (e.g. scale 2 on the 8x16 font gives
 * 16x32 glyphs). Opaque only (no transparent mode) - used for the
 * banner and info pages. */
void LCD_DisplayCharScaled(uint16_t x, uint16_t y, uint8_t c, uint16_t scale)
{
    uint16_t Buff[8 * 16 * 4];        /* holds up to 2x of the 8x16 font */
    uint16_t bytesPerRow;
    uint16_t W, H;
    uint16_t index = 0;

    if ((s_AsciiFont == NULL) || (c < 0x20U) || (c > 0x7EU) ||
        (scale == 0U) || (scale > 2U))
    {
        return;
    }
    c -= 0x20U;

    bytesPerRow = s_AsciiFont->Sizes / s_AsciiFont->Height;
    W = (uint16_t)(s_AsciiFont->Width * scale);
    H = (uint16_t)(s_AsciiFont->Height * scale);

    for (uint16_t row = 0; row < s_AsciiFont->Height; row++)
    {
        for (uint16_t ry = 0; ry < scale; ry++)
        {
            for (uint16_t col = 0; col < s_AsciiFont->Width; col++)
            {
                uint8_t bits = s_AsciiFont->pTable
                    [(uint16_t)c * s_AsciiFont->Sizes
                     + (uint16_t)row * bytesPerRow + (col / 8)];

                uint16_t color = (bits & (uint8_t)(1U << (col % 8)))
                                     ? s_Color : s_BackColor;

                for (uint16_t rx = 0; rx < scale; rx++)
                {
                    Buff[index++] = color;
                }
            }
        }
    }

    LCD_CopyBuffer(x, y, W, H, Buff);
}

void LCD_DisplayStringScaled(uint16_t x, uint16_t y, char *p, uint16_t scale)
{
    const uint16_t advance =
        (uint16_t)((s_AsciiFont != NULL ? s_AsciiFont->Width : 6) * scale);

    while (x < COL && *p != '\0')
    {
        LCD_DisplayCharScaled(x, y, (uint8_t)*p, scale);
        x += advance;
        p++;
    }
}

/* ---- 2D drawing ---- */
void LCD_DrawPoint(uint16_t x, uint16_t y, uint32_t color)
{
    LCD_SetAddress(x, y, x, y);
    LCD_BeginData();
    lcd_write_pixel565(color);
    LCD_EndData();
}

void LCD_DrawLine_V(uint16_t x, uint16_t y, uint16_t height)
{
    LCD_SetAddress(x, y, x, (uint16_t)(y + height - 1));
    LCD_BeginData();
    while (height--)
    {
        lcd_write_pixel565(s_Color);
    }
    LCD_EndData();
}

void LCD_DrawLine_H(uint16_t x, uint16_t y, uint16_t width)
{
    LCD_SetAddress(x, y, (uint16_t)(x + width - 1), y);
    LCD_BeginData();
    while (width--)
    {
        lcd_write_pixel565(s_Color);
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
        lcd_write_pixel565(s_Color);
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
        lcd_write_pixel565(*data);   /* RGB565 -> RGB666, 3 bytes */
        data++;
    }
    LCD_EndData();
}