/* lcd.c - ILI9341 2.8" TFTLCD cacheless 16-bit FSMC parallel driver
 * (apollo-f429 port). Bus: FMC NOR/SRAM bank1 NE1 (0x60000000), 16-bit data,
 * A18 = RS via LCD_BASE 0x60000000|0x7FFFE. Ported from the vendored ALIENTEK
 * Apollo tft_lcd_test example, trimmed to ILI9341 (0x9341) only, with the
 * st7789-style drawing API the test patterns use layered on top.
 */

#include "lcd.h"
#include "board.h"
#include "sys_compat.h"
#include "stm32f4xx_hal.h"

lcd_dev_t lcddev;
uint32_t  POINT_COLOR = 0x00F80000UL;   /* red (RGB888) */
uint32_t  BACK_COLOR  = 0x00FFFFFFUL;   /* white (RGB888) */

/* ---- st7789-style state ---- */
static uint16_t s_Color      = WHITE;
static uint16_t s_BackColor  = BLACK;
static uint8_t  s_Transparent = 0;
static pFONT  *s_AsciiFont   = NULL;

/* =====================================================================
   FMC bank1 NE1 16-bit GPIO + controller init (vendor HAL_SRAM_MspInit +
   LCD_Init, register level - no HAL SRAM driver needed).
   ===================================================================== */
static void FMC_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_FMC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_HIGH;
    gpio.Alternate = GPIO_AF12_FMC;

    /* PD0,1,4,5,7,8,9,10,13,14,15  (D2,D3,NOE,NWE,NE1,D13-15,A18 + D0/D1) */
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_7 |
               GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_13 |
               GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &gpio);

    /* PE7..PE15 (D4..D12) */
    gpio.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |
               GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 |
               GPIO_PIN_15;
    HAL_GPIO_Init(GPIOE, &gpio);
}

static void LCD_FSMCInit(void)
{
    FMC_NORSRAM_InitTypeDef ini = {0};
    FMC_NORSRAM_TimingTypeDef rd = {0}, wr = {0};

    FMC_GPIO_Init();

    ini.NSBank             = FMC_NORSRAM_BANK1;
    ini.DataAddressMux     = FMC_DATA_ADDRESS_MUX_DISABLE;
    ini.MemoryType         = FMC_MEMORY_TYPE_SRAM;
    ini.MemoryDataWidth    = FMC_NORSRAM_MEM_BUS_WIDTH_16;
    ini.BurstAccessMode    = FMC_BURST_ACCESS_MODE_DISABLE;
    ini.WaitSignalPolarity = FMC_WAIT_SIGNAL_POLARITY_LOW;
    ini.WaitSignalActive   = FMC_WAIT_TIMING_BEFORE_WS;
    ini.WriteOperation     = FMC_WRITE_OPERATION_ENABLE;
    ini.WaitSignal         = FMC_WAIT_SIGNAL_DISABLE;
    ini.ExtendedMode       = FMC_EXTENDED_MODE_ENABLE;
    ini.AsynchronousWait   = FMC_ASYNCHRONOUS_WAIT_DISABLE;
    ini.WriteBurst         = FMC_WRITE_BURST_DISABLE;
    ini.ContinuousClock    = FMC_CONTINUOUS_CLOCK_SYNC_ASYNC;

    /* read: ADDSET=15, DATAST=70 (slow, safe); write: ADDSET=15, DATAST=15 */
    rd.AddressSetupTime = 0x0F;
    rd.AddressHoldTime  = 0x00;
    rd.DataSetupTime    = 0x46;
    rd.AccessMode       = FMC_ACCESS_MODE_A;
    wr = rd;
    wr.DataSetupTime    = 0x0F;

    (void)FMC_NORSRAM_Init(FMC_NORSRAM_DEVICE, &ini);
    (void)FMC_NORSRAM_Timing_Init(FMC_NORSRAM_DEVICE, &rd, FMC_NORSRAM_BANK1);
    (void)FMC_NORSRAM_Extended_Timing_Init(FMC_NORSRAM_EXTENDED_DEVICE, &wr,
                                           FMC_NORSRAM_BANK1, FMC_EXTENDED_MODE_ENABLE);

    /* enable the NOR/SRAM bank 1 chip select (MBKEN in BCR1/BTCR[0]) */
    FMC_Bank1->BTCR[FMC_NORSRAM_BANK1] |= FMC_BCR1_MBKEN;
}

/* =====================================================================
   Vendor-style register/data/memory primitives
   ===================================================================== */
static void LCD_WR_REG(uint16_t regval)
{
    LCD->LCD_REG = regval;
}

static void LCD_WR_DATA(uint16_t data)
{
    LCD->LCD_RAM = data;
}

static uint16_t LCD_RD_DATA(void)
{
    return LCD->LCD_RAM;
}

void LCD_WriteReg(uint16_t reg, uint16_t val)
{
    LCD->LCD_REG = reg;
    LCD->LCD_RAM = val;
}

uint16_t LCD_ReadReg(uint16_t reg)
{
    LCD_WR_REG(reg);
    delay_us(5);
    return LCD_RD_DATA();
}

void LCD_WriteRAM_Prepare(void)
{
    LCD->LCD_REG = lcddev.wramcmd;
}

void LCD_WriteRAM(uint16_t rgb)
{
    LCD->LCD_RAM = rgb;
}

/* =====================================================================
   ILI9341 initialization + ID read (vendor LCD_Init, 0x9341 branch)
   ===================================================================== */
static uint16_t LCD_ReadID(void)
{
    uint16_t id;

    LCD_WR_REG(0xD3);                        /* read ID command */
    LCD_RD_DATA();                           /* dummy */
    LCD_RD_DATA();                           /* 0x00 */
    id  = LCD_RD_DATA() << 8;                /* high byte 0x93 */
    id |= LCD_RD_DATA();                     /* low byte  0x41 */
    return id;
}

void LCD_Init(void)
{
    GPIO_InitTypeDef g = {0};

    /* Backlight: PB5 push-pull output, high = on (vendored LCD_LED=PB5). */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    g.Pin   = GPIO_PIN_5;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_HIGH;
    HAL_GPIO_Init(GPIOB, &g);
    PBout(5) = 1;

    LCD_FSMCInit();
    delay_ms(50);

    lcddev.id = LCD_ReadID();
    if (lcddev.id != 0x9341U)
    {
        printf("LCD ID:0x%04X (expected ILI9341 0x9341)\r\n", lcddev.id);
    }

    /* ILI9341 init sequence (vendor tft_lcd_test, 0x9341 branch, verbatim). */
    LCD_WR_REG(0xCF);   LCD_WR_DATA(0x00); LCD_WR_DATA(0xC1); LCD_WR_DATA(0x30);
    LCD_WR_REG(0xED);   LCD_WR_DATA(0x64); LCD_WR_DATA(0x03); LCD_WR_DATA(0x12);
                        LCD_WR_DATA(0x81);
    LCD_WR_REG(0xE8);   LCD_WR_DATA(0x85); LCD_WR_DATA(0x10); LCD_WR_DATA(0x7A);
    LCD_WR_REG(0xCB);   LCD_WR_DATA(0x39); LCD_WR_DATA(0x2C); LCD_WR_DATA(0x00);
                        LCD_WR_DATA(0x34); LCD_WR_DATA(0x02);
    LCD_WR_REG(0xF7);   LCD_WR_DATA(0x20);
    LCD_WR_REG(0xEA);   LCD_WR_DATA(0x00); LCD_WR_DATA(0x00);

    LCD_WR_REG(0xC0);   LCD_WR_DATA(0x1B);   /* power control 1 */
    LCD_WR_REG(0xC1);   LCD_WR_DATA(0x01);   /* power control 2 */
    LCD_WR_REG(0xC5);   LCD_WR_DATA(0x30); LCD_WR_DATA(0x30);  /* VCM */
    LCD_WR_REG(0xC7);   LCD_WR_DATA(0xB7);   /* VCM control 2 */

    LCD_WR_REG(0x36);   LCD_WR_DATA(0x48);   /* MADCTL: BGR + row swap */
    LCD_WR_REG(0x3A);   LCD_WR_DATA(0x55);   /* 16bit/pixel */

    LCD_WR_REG(0xB1);   LCD_WR_DATA(0x00); LCD_WR_DATA(0x1A);  /* frame rate */
    LCD_WR_REG(0xB6);   LCD_WR_DATA(0x0A); LCD_WR_DATA(0xA2);  /* display func */
    LCD_WR_REG(0xF2);   LCD_WR_DATA(0x00);  /* 3Gamma disable */
    LCD_WR_REG(0x26);   LCD_WR_DATA(0x01);  /* gamma curve 1 */

    LCD_WR_REG(0xE0);   LCD_WR_DATA(0x0F); LCD_WR_DATA(0x2A); LCD_WR_DATA(0x28);
                        LCD_WR_DATA(0x08); LCD_WR_DATA(0x0E); LCD_WR_DATA(0x08);
                        LCD_WR_DATA(0x54); LCD_WR_DATA(0xA9); LCD_WR_DATA(0x43);
                        LCD_WR_DATA(0x0A); LCD_WR_DATA(0x0F); LCD_WR_DATA(0x00);
                        LCD_WR_DATA(0x00); LCD_WR_DATA(0x00); LCD_WR_DATA(0x00);
    LCD_WR_REG(0xE1);   LCD_WR_DATA(0x00); LCD_WR_DATA(0x15); LCD_WR_DATA(0x17);
                        LCD_WR_DATA(0x07); LCD_WR_DATA(0x11); LCD_WR_DATA(0x06);
                        LCD_WR_DATA(0x2B); LCD_WR_DATA(0x56); LCD_WR_DATA(0x3C);
                        LCD_WR_DATA(0x05); LCD_WR_DATA(0x10); LCD_WR_DATA(0x0F);
                        LCD_WR_DATA(0x3F); LCD_WR_DATA(0x3F); LCD_WR_DATA(0x0F);

    LCD_WR_REG(0x2B);   LCD_WR_DATA(0x00); LCD_WR_DATA(0x00);
                        LCD_WR_DATA(0x01); LCD_WR_DATA(0x3F);   /* y: 0..319 */
    LCD_WR_REG(0x2A);   LCD_WR_DATA(0x00); LCD_WR_DATA(0x00);
                        LCD_WR_DATA(0x00); LCD_WR_DATA(0xEF);   /* x: 0..239 */

    LCD_WR_REG(0x11);   delay_ms(120);      /* sleep out */
    LCD_WR_REG(0x29);                       /* display on */

    /* fast-write timing tweak (vendor) */
    FMC_Bank1E->BWTR[0] &= ~(0xFUL << 0);   /* ADDSET */
    FMC_Bank1E->BWTR[0] &= ~(0xFUL << 8);   /* DATAST */
    FMC_Bank1E->BWTR[0] |=  4UL << 0;
    FMC_Bank1E->BWTR[0] |=  4UL << 8;

    LCD_Display_Dir(0);      /* portrait 240x320 */
}

void LCD_Display_Dir(uint8_t dir)
{
    uint16_t temp;

    lcddev.dir = dir;
    if (dir == 0)                 /* portrait */
    {
        lcddev.width  = 240;
        lcddev.height = 320;
    }
    else                          /* landscape */
    {
        lcddev.width  = 320;
        lcddev.height = 240;
    }
    lcddev.wramcmd  = 0x2C;
    lcddev.setxcmd  = 0x2A;
    lcddev.setycmd  = 0x2B;
    LCD_Scan_Dir(DFT_SCAN_DIR);
}

void LCD_Scan_Dir(uint8_t dir)
{
    uint16_t regval = 0;
    uint16_t temp;

    switch (dir)
    {
    case L2R_U2D: regval |= (0 << 7) | (0 << 6) | (0 << 5); break;
    case L2R_D2U: regval |= (1 << 7) | (0 << 6) | (0 << 5); break;
    case R2L_U2D: regval |= (0 << 7) | (1 << 6) | (0 << 5); break;
    case R2L_D2U: regval |= (1 << 7) | (1 << 6) | (0 << 5); break;
    case U2D_L2R: regval |= (0 << 7) | (0 << 6) | (1 << 5); break;
    case U2D_R2L: regval |= (0 << 7) | (1 << 6) | (1 << 5); break;
    case D2U_L2R: regval |= (1 << 7) | (0 << 6) | (1 << 5); break;
    case D2U_R2L: regval |= (1 << 7) | (1 << 6) | (1 << 5); break;
    }
    regval |= 0x08;               /* BGR */
    LCD_WriteReg(0x36, regval);

    if (regval & 0x20)
    {
        if (lcddev.width < lcddev.height)
        {
            temp = lcddev.width;  lcddev.width = lcddev.height;
            lcddev.height = temp;
        }
    }
    else
    {
        if (lcddev.width > lcddev.height)
        {
            temp = lcddev.width;  lcddev.width = lcddev.height;
            lcddev.height = temp;
        }
    }

    LCD_WR_REG(lcddev.setxcmd);
    LCD_WR_DATA(0); LCD_WR_DATA(0);
    LCD_WR_DATA((lcddev.width - 1) >> 8);  LCD_WR_DATA((lcddev.width - 1) & 0xFF);
    LCD_WR_REG(lcddev.setycmd);
    LCD_WR_DATA(0); LCD_WR_DATA(0);
    LCD_WR_DATA((lcddev.height - 1) >> 8); LCD_WR_DATA((lcddev.height - 1) & 0xFF);
}

/* =====================================================================
   Window / cursor
   ===================================================================== */
void LCD_SetCursor(uint16_t x, uint16_t y)
{
    LCD_WR_REG(lcddev.setxcmd);
    LCD_WR_DATA(x >> 8); LCD_WR_DATA(x & 0xFF);
    LCD_WR_REG(lcddev.setycmd);
    LCD_WR_DATA(y >> 8); LCD_WR_DATA(y & 0xFF);
}

void LCD_Set_Window(uint16_t sx, uint16_t sy, uint16_t w, uint16_t h)
{
    uint16_t twidth  = sx + w - 1;
    uint16_t theight = sy + h - 1;

    LCD_WR_REG(lcddev.setxcmd);
    LCD_WR_DATA(sx >> 8); LCD_WR_DATA(sx & 0xFF);
    LCD_WR_DATA(twidth >> 8); LCD_WR_DATA(twidth & 0xFF);
    LCD_WR_REG(lcddev.setycmd);
    LCD_WR_DATA(sy >> 8); LCD_WR_DATA(sy & 0xFF);
    LCD_WR_DATA(theight >> 8); LCD_WR_DATA(theight & 0xFF);
}

/* =====================================================================
   Vendor drawing / text
   ===================================================================== */
void LCD_Clear(uint32_t color)
{
    uint32_t total = (uint32_t)lcddev.width * lcddev.height;
    LCD_SetCursor(0, 0);
    LCD_WriteRAM_Prepare();
    while (total--)
    {
        LCD->LCD_RAM = (uint16_t)color;
    }
}

void LCD_Fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint32_t color)
{
    uint16_t xlen = ex - sx + 1;
    uint16_t i, j;

    for (i = sy; i <= ey; i++)
    {
        LCD_SetCursor(sx, i);
        LCD_WriteRAM_Prepare();
        for (j = 0; j < xlen; j++)
        {
            LCD->LCD_RAM = (uint16_t)color;
        }
    }
}

void LCD_Color_Fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color)
{
    uint16_t w = ex - sx + 1;
    uint16_t h = ey - sy + 1;
    uint16_t i, j;

    for (i = 0; i < h; i++)
    {
        LCD_SetCursor(sx, sy + i);
        LCD_WriteRAM_Prepare();
        for (j = 0; j < w; j++)
        {
            LCD->LCD_RAM = color[i * w + j];
        }
    }
}

void LCD_DrawPoint(uint16_t x, uint16_t y)
{
    LCD_SetCursor(x, y);
    LCD_WriteRAM_Prepare();
    LCD->LCD_RAM = (uint16_t)POINT_COLOR;
}

void LCD_Fast_DrawPoint(uint16_t x, uint16_t y, uint32_t color)
{
    LCD_SetCursor(x, y);
    LCD_WriteRAM_Prepare();
    LCD->LCD_RAM = (uint16_t)color;
}

uint32_t LCD_ReadPoint(uint16_t x, uint16_t y)
{
    uint16_t r, g, b;

    if (x >= lcddev.width || y >= lcddev.height) { return 0; }
    LCD_SetCursor(x, y);
    LCD_WR_REG(0x2E);
    r = LCD_RD_DATA();          /* dummy */
    r = LCD_RD_DATA();
    g = LCD_RD_DATA();
    b = LCD_RD_DATA();
    return ((((r >> 11) << 11) | ((g >> 10) << 5) | (b >> 11)));
}

void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    uint16_t t;
    int xerr = 0, yerr = 0, delta_x, delta_y, distance;
    int incx, incy, uRow, uCol;

    delta_x = (int)x2 - (int)x1;
    delta_y = (int)y2 - (int)y1;
    uRow = x1; uCol = y1;

    if (delta_x > 0)      { incx = 1; }
    else if (delta_x == 0){ incx = 0; }
    else                  { incx = -1; delta_x = -delta_x; }
    if (delta_y > 0)      { incy = 1; }
    else if (delta_y == 0){ incy = 0; }
    else                  { incy = -1; delta_y = -delta_y; }

    distance = (delta_x > delta_y) ? delta_x : delta_y;
    for (t = 0; t <= distance + 1; t++)
    {
        LCD_DrawPoint((uint16_t)uRow, (uint16_t)uCol);
        xerr += delta_x;
        yerr += delta_y;
        if (xerr > distance) { xerr -= distance; uRow += incx; }
        if (yerr > distance) { yerr -= distance; uCol += incy; }
    }
}

void LCD_DrawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    LCD_DrawLine(x1, y1, x2, y1);
    LCD_DrawLine(x1, y1, x1, y2);
    LCD_DrawLine(x1, y2, x2, y2);
    LCD_DrawLine(x2, y1, x2, y2);
}

void LCD_Draw_Circle(uint16_t x0, uint16_t y0, uint8_t r)
{
    int a = 0, b = r, di = 3 - (r << 1);

    while (a <= b)
    {
        LCD_DrawPoint(x0 + a, y0 - b);
        LCD_DrawPoint(x0 + b, y0 - a);
        LCD_DrawPoint(x0 + b, y0 + a);
        LCD_DrawPoint(x0 + a, y0 + b);
        LCD_DrawPoint(x0 - a, y0 + b);
        LCD_DrawPoint(x0 - b, y0 + a);
        LCD_DrawPoint(x0 - a, y0 - b);
        LCD_DrawPoint(x0 - b, y0 - a);
        a++;
        if (di < 0) { di += 4 * a + 6; }
        else        { di += 10 + 4 * (a - b); b--; }
    }
}

void LCD_ShowChar(uint16_t x, uint16_t y, uint8_t num, uint8_t size, uint8_t mode)
{
    /* Route through the proven st7789-style text renderer. mode != 0 is
     * transparent (no background box), mode == 0 draws the BACK_COLOR box -
     * exactly the vendored LCD_ShowChar semantics, using the same LSB-first
     * font tables the info page uses. */
    LCD_SetAsciiFont((size == 12) ? &ASCII_Font12 : &ASCII_Font16);
    LCD_SetColor(POINT_COLOR);
    LCD_SetBackColor(BACK_COLOR);
    LCD_ShowTransparent((mode != 0) ? 1U : 0U);
    LCD_DisplayChar(x, y, num);
    LCD_ShowTransparent(0);
}

static uint32_t LCD_Pow(uint8_t m, uint8_t n)
{
    uint32_t result = 1;
    while (n--) { result *= m; }
    return result;
}

void LCD_ShowNum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size)
{
    uint8_t t, temp;
    uint8_t enshow = 0;

    for (t = 0; t < len; t++)
    {
        temp = (uint8_t)((num / LCD_Pow(10, len - t - 1)) % 10);
        if (enshow == 0 && t < (len - 1))
        {
            if (temp == 0)
            {
                LCD_ShowChar(x + (size / 2) * t, y, ' ', size, 0);
                continue;
            }
            enshow = 1;
        }
        LCD_ShowChar(x + (size / 2) * t, y, temp + '0', size, 0);
    }
}

void LCD_ShowxNum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint8_t mode)
{
    uint8_t t, temp;
    uint8_t enshow = 0;

    for (t = 0; t < len; t++)
    {
        temp = (uint8_t)((num / LCD_Pow(10, len - t - 1)) % 10);
        if (enshow == 0 && t < (len - 1))
        {
            if (temp == 0)
            {
                if (mode & 0x80) { LCD_ShowChar(x + (size / 2) * t, y, '0', size, mode & 0x01); }
                else             { LCD_ShowChar(x + (size / 2) * t, y, ' ', size, mode & 0x01); }
                continue;
            }
            enshow = 1;
        }
        LCD_ShowChar(x + (size / 2) * t, y, temp + '0', size, mode & 0x01);
    }
}

void LCD_ShowString(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                    uint8_t size, uint8_t *p)
{
    uint8_t x0 = x;
    width  += x;
    height += y;

    while ((*p <= '~') && (*p >= ' '))
    {
        if (x >= width) { x = x0; y += size; }
        if (y >= height) { break; }
        LCD_ShowChar(x, y, *p, size, 1);   /* transparent (no BACK_COLOR box) */
        x += size / 2;
        p++;
    }
}

/* =====================================================================
   st7789-style drawing API
   ===================================================================== */
void LCD_SetColor(uint32_t rgb888)
{
    /* st7789 patterns pass 24-bit RGB888; convert to the 16-bit word the
     * parallel ILI9341 wants, and mirror it into POINT_COLOR so the vendored
     * LCD_DrawLine/LCD_DrawPoint (which read POINT_COLOR) draw the same color. */
    uint16_t r = (uint16_t)((rgb888 & 0x00F80000UL) >> 8);
    uint16_t g = (uint16_t)((rgb888 & 0x0000FC00UL) >> 5);
    uint16_t b = (uint16_t)((rgb888 & 0x000000F8UL) >> 3);
    s_Color = (uint16_t)(r | g | b);
    POINT_COLOR = s_Color;
}

void LCD_SetBackColor(uint32_t rgb888)
{
    uint16_t r = (uint16_t)((rgb888 & 0x00F80000UL) >> 8);
    uint16_t g = (uint16_t)((rgb888 & 0x0000FC00UL) >> 5);
    uint16_t b = (uint16_t)((rgb888 & 0x000000F8UL) >> 3);
    s_BackColor = (uint16_t)(r | g | b);
    BACK_COLOR = s_BackColor;
}
void LCD_SetAsciiFont(pFONT *font)      { s_AsciiFont = font; }
void LCD_ShowTransparent(uint8_t mode)  { s_Transparent = mode; }

void LCD_SetAddress(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    LCD_Set_Window(x1, y1, x2 - x1 + 1, y2 - y1 + 1);
    LCD->LCD_REG = lcddev.wramcmd;       /* leave cursor ready for data */
}

void LCD_ClearBg(void)
{
    uint32_t n = (uint32_t)COL * ROW;
    LCD_SetAddress(0, 0, COL - 1, ROW - 1);
    while (n--) { LCD->LCD_RAM = s_BackColor; }
}

void LCD_ClearRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    uint32_t n = (uint32_t)width * height;
    if (n == 0) { return; }
    LCD_SetAddress(x, y, x + width - 1, y + height - 1);
    while (n--) { LCD->LCD_RAM = s_BackColor; }
}

void LCD_FillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    uint32_t n = (uint32_t)width * height;
    if (n == 0) { return; }
    LCD_SetAddress(x, y, x + width - 1, y + height - 1);
    while (n--) { LCD->LCD_RAM = s_Color; }
}

void LCD_DrawPointC(uint16_t x, uint16_t y, uint32_t color)
{
    LCD_Fast_DrawPoint(x, y, (uint16_t)color);
}

void LCD_DrawLine_H(uint16_t x, uint16_t y, uint16_t width)
{
    LCD_SetAddress(x, y, x + width - 1, y);
    while (width--) { LCD->LCD_RAM = s_Color; }
}

void LCD_DrawLine_V(uint16_t x, uint16_t y, uint16_t height)
{
    LCD_SetAddress(x, y, x, y + height - 1);
    while (height--) { LCD->LCD_RAM = s_Color; }
}

void LCD_DrawLineXY(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    LCD_DrawLine(x1, y1, x2, y2);        /* uses POINT_COLOR == routed by caller */
}

void LCD_DrawRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    LCD_DrawLine_H(x, y, width);
    LCD_DrawLine_H(x, y + height - 1, width);
    LCD_DrawLine_V(x, y, height);
    LCD_DrawLine_V(x + width - 1, y, height);
}

void LCD_DrawCircle(uint16_t x, uint16_t y, uint16_t r)
{
    int16_t Xadd = -(int16_t)r, Yadd = 0, err = 2 - 2 * (int16_t)r, e2;

    do
    {
        LCD_DrawPointC((uint16_t)(x - Xadd), (uint16_t)(y + Yadd), s_Color);
        LCD_DrawPointC((uint16_t)(x + Xadd), (uint16_t)(y + Yadd), s_Color);
        LCD_DrawPointC((uint16_t)(x + Xadd), (uint16_t)(y - Yadd), s_Color);
        LCD_DrawPointC((uint16_t)(x - Xadd), (uint16_t)(y - Yadd), s_Color);
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
    } while (Xadd <= 0);
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
            LCD_DrawLine_V((uint16_t)(x - CurX), (uint16_t)(y - CurY), (uint16_t)(2 * CurY));
            LCD_DrawLine_V((uint16_t)(x + CurX), (uint16_t)(y - CurY), (uint16_t)(2 * CurY));
        }
        if (CurX > 0)
        {
            LCD_DrawLine_V((uint16_t)(x - CurY), (uint16_t)(y - CurX), (uint16_t)(2 * CurX));
            LCD_DrawLine_V((uint16_t)(x + CurY), (uint16_t)(y - CurX), (uint16_t)(2 * CurX));
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

void LCD_CopyBuffer(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t *data)
{
    uint32_t n = (uint32_t)width * height;
    LCD_SetAddress(x, y, x + width - 1, y + height - 1);
    while (n--) { LCD->LCD_RAM = *data++; }
}

void LCD_DisplayChar(uint16_t x, uint16_t y, uint8_t c)
{
    uint16_t Buff[8 * 16];           /* big enough for both 6x12 and 8x16 fonts */
    uint16_t bytesPerRow;

    if (s_AsciiFont == NULL || c < 0x20 || c > 0x7E) { return; }
    c -= 0x20;
    bytesPerRow = (uint16_t)(s_AsciiFont->Sizes / s_AsciiFont->Height);

    for (uint16_t row = 0; row < s_AsciiFont->Height; row++)
    {
        for (uint16_t col = 0; col < s_AsciiFont->Width; col++)
        {
            uint8_t disChar = s_AsciiFont->pTable[(uint16_t)c * s_AsciiFont->Sizes
                              + (uint16_t)row * bytesPerRow + (col / 8)];
            uint16_t pix = (disChar & (1U << (col % 8))) ? s_Color : s_BackColor;

            if (s_Transparent)
            {
                if (pix == s_Color)
                {
                    LCD_DrawPointC(x + col, y + row, pix);
                }
            }
            else
            {
                Buff[row * s_AsciiFont->Width + col] = pix;
            }
        }
    }

    if (!s_Transparent)
    {
        LCD_CopyBuffer(x, y, s_AsciiFont->Width, s_AsciiFont->Height, Buff);
    }
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

/* =====================================================================
   Vendor TEST_STAND screens (BlockWrite + raster dump)
   ===================================================================== */
void BlockWrite(uint16_t Xstart, uint16_t Xend, uint16_t Ystart, uint16_t Yend)
{
    LCD_SetAddress(Xstart, Ystart, Xend, Yend);
}

void DispColor(uint32_t color)
{
    uint32_t n = (uint32_t)COL * ROW;
    BlockWrite(COL_Pre, COL + COL_Pre - 1, ROW_Pre, ROW + ROW_Pre - 1);
    while (n--) { LCD->LCD_RAM = (uint16_t)color; }
}

void DispFrame(void)
{
    int i, j;
    BlockWrite(COL_Pre, COL + COL_Pre - 1, ROW_Pre, ROW + ROW_Pre - 1);
    LCD->LCD_RAM = 0xF800;
    for (i = 0; i < COL - 2; i++) { LCD->LCD_RAM = 0xFFFF; }
    LCD->LCD_RAM = 0x001F;
    for (j = 0; j < ROW - 2; j++)
    {
        LCD->LCD_RAM = 0xF800;
        for (i = 0; i < COL - 2; i++) { LCD->LCD_RAM = 0x0000; }
        LCD->LCD_RAM = 0x001F;
    }
    LCD->LCD_RAM = 0xF800;
    for (i = 0; i < COL - 2; i++) { LCD->LCD_RAM = 0xFFFF; }
    LCD->LCD_RAM = 0x001F;
}

void DispGrayHor16(void)
{
    int i, j, k;
    BlockWrite(COL_Pre, COL + COL_Pre - 1, ROW_Pre, ROW + ROW_Pre - 1);
    for (i = 0; i < ROW; i++)
    {
        for (j = 0; j < COL % 16; j++) { LCD->LCD_RAM = 0; }
        for (j = 0; j < 16; j++)
        {
            for (k = 0; k < COL / 16; k++)
            {
                LCD->LCD_RAM = (uint16_t)(((((j * 2) << 3) | ((j * 4) >> 3)) << 8) |
                                          (((j * 4) << 5) | (j * 2)));
            }
        }
    }
}

void DispBand(void)
{
    static const uint32_t color[8] = { 0xF800, 0xF800, 0x07E0, 0x07E0,
                                       0x001F, 0x001F, 0xFFFF, 0xFFFF };
    int i, j, k;
    BlockWrite(COL_Pre, COL + COL_Pre - 1, ROW_Pre, ROW + ROW_Pre - 1);
    for (i = 0; i < 8; i++)
    {
        for (j = 0; j < ROW / 8; j++)
        {
            for (k = 0; k < COL; k++) { LCD->LCD_RAM = (uint16_t)color[i]; }
        }
    }
    for (j = 0; j < ROW % 8; j++)
    {
        for (k = 0; k < COL; k++) { LCD->LCD_RAM = (uint16_t)color[7]; }
    }
}

void StopDelay(uint16_t ms)
{
    HAL_Delay(ms);
}