/*
  lcd.c - ST7796S 4.0" 320x480 IPS LCD driver (nucleo-f413 port,
  CL40BC267-40A / "40002" module). Vendor-verbatim ST7796S init
  (F0=C3/96 unlock, B4/B7/E8/C1/C2/C5 power+timing, gamma tables,
  MADCTL 0x48, RGB565 COLMOD 0x55, sleep out + 120 ms, inversion on,
  display on) over 4-wire SPI - the command byte goes out with DC low,
  every data byte with DC high.

  Unlike the ILI9488 module next door, the ST7796S accepts 16-bit
  RGB565 pixels in 4-line serial mode (COLMOD 0x3A = 0x55, two bytes
  per pixel, MSB first) - a third fewer bytes on the wire and no per
  pixel color conversion.

  Wiring: SCL=PA5, SDA=PA7, DC=PD15, RST=PF12, CS=PD14, BL=PE9
  (TIM1_CH1 PWM). Touch reserved: INT=PG2, RST=PG3, SCL=PB8, SDA=PB9.
  Vendor example: D:\module_db\ips395-st7796-captouch\CL40BC267-40A_IPS_CTP
  (STM32F1 / STC32 demos + ST7796S_BOE3.98_IPS_Init.txt).
*/

#include <string.h>
#include <stdio.h>
#include "board.h"
#include "lcd.h"
#include "interface.h"
#include "blockwrite.h"

/* ---- runtime drawing window ----
 * Every draw is relative to this window (origin + size). It defaults to
 * the full panel and can be shrunk at runtime; the surrounding area keeps
 * its previous content. */
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
   Init sequence - ST7796S, vendor-verbatim (ST7796S_BOE3.98_IPS_Init.txt
   and the 4SPI demo's LCD_Init): F0=C3/F0=96 unlock, MADCTL, RGB565
   COLMOD, power/VCOM/timing (B4/B7/E8/C1/C2/C5), gamma (E0/E1),
   F0=3C/F0=69 lock, inversion on, sleep out + 120 ms, display on.

   MADCTL (0x36) controls the mirrors/rotation - the one thing that has
   to be retuned per module. Bit map: MY=7, MX=6, MV=5, ML=4, RGB=3,
   MH=2. Vendor portrait (0 deg) = 0x48 (MX + RGB). Override at build
   time with -DLCD_MADCTL=0x.. , e.g.
     0x08 none            0x48 MX            0x88 MX+MY (180 deg)
     0xC8 MY              0x28 MV (landscape) 0xE8 MV+MX+MY
   Clear bit 3 (RGB) if red and blue come out swapped.
   ===================================================================== */
void LCD_IC_Init(void)
{
    WriteComm(0x11);              /* Sleep out */
    HAL_Delay(120);

    WriteComm(0xF0);              /* Command2 BK0 select */
    WriteData(0xC3);
    WriteComm(0xF0);
    WriteData(0x96);

    WriteComm(0x36);              /* MADCTL */
    WriteData(LCD_MADCTL);

    WriteComm(0x3A);              /* COLMOD: 16-bit RGB565 (2 bytes/px) */
    WriteData(0x55);

    WriteComm(0xB4);              /* Display inversion control */
    WriteData(0x01);

    WriteComm(0xB7);              /* Entry mode */
    WriteData(0xC6);

    WriteComm(0xE8);              /* Gated/gated-source timing */
    WriteData(0x40);
    WriteData(0x8A);
    WriteData(0x00);
    WriteData(0x00);
    WriteData(0x29);
    WriteData(0x19);
    WriteData(0xA5);
    WriteData(0x33);

    WriteComm(0xC1);              /* Power control 2 */
    WriteData(0x06);

    WriteComm(0xC2);              /* Power control 3 */
    WriteData(0xA7);

    WriteComm(0xC5);              /* VCOM */
    WriteData(0x18);

    WriteComm(0xE0);              /* Positive gamma */
    WriteData(0xF0); WriteData(0x09); WriteData(0x0B); WriteData(0x06);
    WriteData(0x04); WriteData(0x15); WriteData(0x2F); WriteData(0x54);
    WriteData(0x42); WriteData(0x3C); WriteData(0x17); WriteData(0x14);
    WriteData(0x18); WriteData(0x1B);

    WriteComm(0xE1);              /* Negative gamma */
    WriteData(0xF0); WriteData(0x09); WriteData(0x0B); WriteData(0x06);
    WriteData(0x04); WriteData(0x03); WriteData(0x2D); WriteData(0x43);
    WriteData(0x42); WriteData(0x3B); WriteData(0x16); WriteData(0x14);
    WriteData(0x17); WriteData(0x1B);

    WriteComm(0xF0);              /* Command2 BK1 select */
    WriteData(0x3C);
    WriteComm(0xF0);
    WriteData(0x69);

    WriteComm(0x21);              /* Display inversion on (vendor) */
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
   ST7796S serial mode pixels are 16-bit RGB565 (COLMOD 0x3A = 0x55):
   two bytes per pixel, high byte first.
   ===================================================================== */
static void lcd_write_pixel565(uint16_t c565)
{
    LCD_WritePixel16(c565);
}

/* Stream npix pixels of one RGB565 color into the open window. */
static void lcd_fill_area(uint32_t npix, uint16_t c565)
{
    LCD_BeginData();
    LCD_StreamColor16(c565, npix);
    LCD_EndData();
}

void DispColor(uint32_t color)
{
    uint32_t n = (uint32_t)LCD_W() * LCD_H();

    BlockWrite(0, LCD_W() - 1U, 0, LCD_H() - 1U);
    lcd_fill_area(n, (uint16_t)color);
}

void DispFrame(void)
{
    /* white interior, red 1-px border - five color streams */
    uint16_t w = LCD_W(), h = LCD_H();

    BlockWrite(0, (uint16_t)(w - 1U), 0, (uint16_t)(h - 1U));
    lcd_fill_area((uint32_t)w * h, 0xFFFFU);            /* white */

    BlockWrite(0, (uint16_t)(w - 1U), 0, 0);
    lcd_fill_area(w, 0xF800U);                          /* top row */

    BlockWrite(0, (uint16_t)(w - 1U),
               (uint16_t)(h - 1U), (uint16_t)(h - 1U));
    lcd_fill_area(w, 0xF800U);                          /* bottom row */

    BlockWrite(0, 0, 1, (uint16_t)(h - 2U));
    lcd_fill_area((uint32_t)(h - 2U), 0xF800U);         /* left column */

    BlockWrite((uint16_t)(w - 1U), (uint16_t)(w - 1U), 1, (uint16_t)(h - 2U));
    lcd_fill_area((uint32_t)(h - 2U), 0xF800U);         /* right column */
}

void DispGrayHor16(void)
{
    /* 16 gray segments per scanline, each one color stream */
    int i, j;
    uint16_t rem = (uint16_t)(LCD_W() % 16);

    BlockWrite(0, LCD_W() - 1U, 0, LCD_H() - 1U);
    LCD_BeginData();
    for (i = 0; i < (int)LCD_H(); i++)
    {
        if (rem != 0U) { LCD_StreamColor16(0x0000U, rem); }
        for (j = 0; j < 16; j++)
        {
            uint16_t c = (uint16_t)(((((j * 2) << 3) | ((j * 4) >> 3)) << 8) |
                                    (((j * 4) << 5) | (j * 2)));
            LCD_StreamColor16(c, (uint32_t)(LCD_W() / 16));
        }
    }
    LCD_EndData();
}

void DispBand(void)
{
    static const uint16_t color[8] = { 0xF800, 0xF800, 0x07E0, 0x07E0,
                                       0x001F, 0x001F, 0xFFFF, 0xFFFF };
    int i;
    uint32_t rows = (uint32_t)(LCD_H() / 8) * LCD_W();

    BlockWrite(0, LCD_W() - 1U, 0, LCD_H() - 1U);
    LCD_BeginData();
    for (i = 0; i < 8; i++)
    {
        LCD_StreamColor16(color[i], rows);
    }
    LCD_StreamColor16(color[7],
                       (uint32_t)(LCD_H() % 8) * LCD_W());
    LCD_EndData();
}

void StopDelay(uint16_t ms)
{
    HAL_Delay(ms);
}

/* =====================================================================
   Colors, addressing, text and 2D drawing - the standard st7789-style
   drawing API, adapted to the 320x480 ST7796S.
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
    LCD_SetAddress(0, 0, (uint16_t)(s_win_w - 1U), (uint16_t)(s_win_h - 1U));
    lcd_fill_area((uint32_t)s_win_w * s_win_h, s_BackColor);
}

void LCD_ClearRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    uint32_t n = (uint32_t)width * height;
    if (n == 0U) { return; }
    LCD_SetAddress(x, y, (uint16_t)(x + width - 1U), (uint16_t)(y + height - 1U));
    lcd_fill_area(n, s_BackColor);
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

/* =====================================================================
   Single-pass page renderer: paints the window background color and
   every requested text line into a per-line raster buffer, then streams
   the whole window as ONE continuous BeginData/EndData burst. Replaces
   the separate clear-pass + per-glyph-window passes (two panel passes
   and dozens of window setups become one).
   Text is opaque (fg glyphs on bg), drawn with the current font at the
   requested integer scale (1 or 2), top-left anchored per line.
   ===================================================================== */
static uint16_t s_page_row[320];      /* one scanline of RGB565      */

void LCD_DrawTextPage(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      uint8_t nlines, const LcdTextLine *lines)
{
    pFONT *font = s_AsciiFont;

    if ((font == NULL) || (w == 0U) || (h == 0U) || (w > 320U))
    {
        return;
    }

    LCD_SetAddress(x, y, (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U));
    LCD_BeginData();

    for (uint16_t py = 0; py < h; py++)
    {
        /* background */
        for (uint16_t px = 0; px < w; px++)
        {
            s_page_row[px] = s_BackColor;
        }

        /* glyphs covering this scanline */
        for (uint8_t li = 0; li < nlines; li++)
        {
            const LcdTextLine *L = &lines[li];
            uint16_t glyphH = (uint16_t)(font->Height * L->scale);

            if (py < L->y || py >= (uint16_t)(L->y + glyphH))
            {
                continue;
            }

            uint16_t fontRow = (uint16_t)((py - L->y) / L->scale);
            uint16_t bytesPerRow = (uint16_t)(font->Sizes / font->Height);
            const char *p = L->text;
            uint16_t cx = L->x;

            while (*p != '\0')
            {
                uint8_t c = (uint8_t)*p;

                if ((c >= 0x20U) && (c <= 0x7EU))
                {
                    uint8_t idx = (uint8_t)(c - 0x20U);
                    uint16_t base = (uint16_t)((uint16_t)idx * font->Sizes
                                   + fontRow * bytesPerRow);

                    for (uint16_t col = 0; col < font->Width; col++)
                    {
                        uint8_t bits = font->pTable
                            [base + (uint16_t)(col / 8U)];
                        uint16_t color = (bits & (uint8_t)(1U << (col % 8U)))
                                             ? s_Color : s_BackColor;

                        for (uint16_t rx = 0; rx < L->scale; rx++)
                        {
                            uint16_t px = (uint16_t)(cx - x
                                         + col * L->scale + rx);
                            if (px < w)
                            {
                                s_page_row[px] = color;
                            }
                        }
                    }
                }

                cx = (uint16_t)(cx + font->Width * L->scale);
                p++;
            }
        }

        /* stream the line: RGB565, 2 bytes per pixel */
        for (uint16_t px = 0; px < w; px++)
        {
            lcd_write_pixel565(s_page_row[px]);
        }
    }

    LCD_EndData();
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
    if (height == 0U) { return; }
    LCD_SetAddress(x, y, x, (uint16_t)(y + height - 1U));
    lcd_fill_area(height, s_Color);
}

void LCD_DrawLine_H(uint16_t x, uint16_t y, uint16_t width)
{
    if (width == 0U) { return; }
    LCD_SetAddress(x, y, (uint16_t)(x + width - 1U), y);
    lcd_fill_area(width, s_Color);
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
    LCD_SetAddress(x, y, (uint16_t)(x + width - 1U), (uint16_t)(y + height - 1U));
    lcd_fill_area(n, s_Color);
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
        lcd_write_pixel565(*data);   /* RGB565, 2 bytes */
        data++;
    }
    LCD_EndData();
}