#include "board_ltdc.h"
#include "board.h"
#include "stm32f4xx_hal_ltdc.h"
#include "stm32f4xx_hal_i2c.h"

void Board_SDRAM_EarlyInit(void);

/* 5-inch 800x480 panel timing (vendor example). */
#define LCD_HBP 46U
#define LCD_VBP 23U
#define LCD_HSW 1U
#define LCD_VSW 3U
#define LCD_HFP 40U
#define LCD_VFP 13U

static LTDC_HandleTypeDef hltdc;

/* ------------------------------------------------------------------ */
/* Touch: bit-banged I2C on PH4 (SCL) / PH5 (SDA). The panel's touch
 * controller reads PID 0x3931 ("91", GT911-family) at 8-bit address 0xBA.
 * The vendor driver explicitly avoids the STM32 hardware I2C for these
 * Goodix controllers ("very error prone"), so we bit-bang the interface. */

#define TOUCH_SCL_PORT GPIOH
#define TOUCH_SCL_PIN  GPIO_PIN_4
#define TOUCH_SDA_PORT GPIOH
#define TOUCH_SDA_PIN  GPIO_PIN_5

#define TOUCH_I2C_ADDR     0xBAU   /* 8-bit write byte (7-bit addr 0x5D) */
#define TOUCH_REG_STATUS   0x814EU
#define TOUCH_REG_DATA     0x814FU
#define TOUCH_REG_VERSION  0x8140U
#define TOUCH_RST_PORT     GPIOD
#define TOUCH_RST_PIN      GPIO_PIN_11
#define TOUCH_INT_PORT     GPIOD
#define TOUCH_INT_PIN      GPIO_PIN_13

/* The touch IC on this panel responds at 8-bit address 0xBA (7-bit 0x5D),
 * selected by holding INT low during reset (verified by probe on hardware). */

static void TouchSCL(int high)
{
    HAL_GPIO_WritePin(TOUCH_SCL_PORT, TOUCH_SCL_PIN,
                      high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void TouchSDA(int high)
{
    HAL_GPIO_WritePin(TOUCH_SDA_PORT, TOUCH_SDA_PIN,
                      high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static int TouchSDARead(void)
{
    return HAL_GPIO_ReadPin(TOUCH_SDA_PORT, TOUCH_SDA_PIN) == GPIO_PIN_SET;
}

static void TouchDelay(void)
{
    volatile uint32_t n = 24;
    while (n-- != 0U) { }
}

static void TouchI2CStart(void)
{
    TouchSDA(1);
    TouchSCL(1);
    TouchDelay();
    TouchSDA(0);
    TouchDelay();
    TouchSCL(0);
    TouchDelay();
}

static void TouchI2CStop(void)
{
    TouchSDA(0);
    TouchSCL(1);
    TouchDelay();
    TouchSDA(1);
    TouchDelay();
}

/* Returns 0 on ACK, non-zero on NACK. */
static int TouchI2CWriteByte(uint8_t data)
{
    for (int bit = 7; bit >= 0; bit--)
    {
        TouchSDA((data >> bit) & 1U);
        TouchDelay();
        TouchSCL(1);
        TouchDelay();
        TouchSCL(0);
        TouchDelay();
    }
    /* release SDA for ACK */
    TouchSDA(1);
    TouchDelay();
    TouchSCL(1);
    TouchDelay();
    int nack = TouchSDARead();
    TouchSCL(0);
    TouchDelay();
    return nack;
}

static uint8_t TouchI2CReadByte(int ack)
{
    uint8_t data = 0;
    TouchSDA(1);
    for (int bit = 7; bit >= 0; bit--)
    {
        TouchSCL(1);
        TouchDelay();
        data = (uint8_t)((data << 1) | (uint8_t)TouchSDARead());
        TouchSCL(0);
        TouchDelay();
    }
    TouchSDA(ack ? 0 : 1);   /* ACK low / NACK high */
    TouchDelay();
    TouchSCL(1);
    TouchDelay();
    TouchSCL(0);
    TouchDelay();
    TouchSDA(1);
    return data;
}

/* Reads `len` bytes from 16-bit register address `reg`. */
static int TouchI2CReadReg16(uint16_t reg, uint8_t *buf, uint16_t len)
{
    TouchI2CStart();
    if (TouchI2CWriteByte(TOUCH_I2C_ADDR) != 0)  /* write address */
    {
        TouchI2CStop();
        return -1;
    }
    TouchI2CWriteByte((uint8_t)(reg >> 8));
    TouchI2CWriteByte((uint8_t)reg);

    TouchI2CStart();
    if (TouchI2CWriteByte(TOUCH_I2C_ADDR | 1U) != 0)  /* read address */
    {
        TouchI2CStop();
        return -1;
    }
    for (uint16_t i = 0; i < len; i++)
    {
        buf[i] = TouchI2CReadByte(i != len - 1);
    }
    TouchI2CStop();
    return 0;
}

static int TouchI2CWriteBuffer(uint16_t reg, const uint8_t *buf, uint16_t len);

static int TouchI2CWriteReg16(uint16_t reg, uint8_t value)
{
    uint8_t buf = value;
    return TouchI2CWriteBuffer(reg, &buf, 1U);
}

/* Writes `len` bytes to 16-bit register address `reg`. */
static int TouchI2CWriteBuffer(uint16_t reg, const uint8_t *buf, uint16_t len)
{
    TouchI2CStart();
    if (TouchI2CWriteByte(TOUCH_I2C_ADDR) != 0)
    {
        TouchI2CStop();
        return -1;
    }
    TouchI2CWriteByte((uint8_t)(reg >> 8));
    TouchI2CWriteByte((uint8_t)reg);
    for (uint16_t i = 0; i < len; i++)
    {
        TouchI2CWriteByte(buf[i]);
    }
    TouchI2CStop();
    return 0;
}

/* Framebuffer: 800x480 RGB888 = 1.125 MiB, placed by the linker in the
 * `.sdram_fb` SDRAM section for both bare and app builds. */
__attribute__((section(".sdram_fb"))) static uint8_t frame_buffer[LCD_WIDTH * LCD_HEIGHT * 3U];
#define FRAME_BUFFER_ADDRESS ((uint32_t)(uintptr_t)frame_buffer)

uint32_t LTDC_Display_FrameBuffer(void)
{
    return FRAME_BUFFER_ADDRESS;
}

static void LtdcGpioInit(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    gpio.Alternate = GPIO_AF14_LTDC;
    gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_8;          /* PH: R2, R3(R1?) keep AF14 set */
    HAL_GPIO_Init(GPIOH, &gpio);
    gpio.Pin = GPIO_PIN_13 | GPIO_PIN_15;                     /* PH: G2, G4 */
    HAL_GPIO_Init(GPIOH, &gpio);
    gpio.Pin = GPIO_PIN_11 | GPIO_PIN_12;                     /* PA: R4, R5 */
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_6;                       /* PE: G0, G1 */
    HAL_GPIO_Init(GPIOE, &gpio);
    gpio.Pin = GPIO_PIN_4;                                    /* PE: B0 */
    HAL_GPIO_Init(GPIOE, &gpio);
    gpio.Pin = GPIO_PIN_0;                                    /* PI: G5 */
    HAL_GPIO_Init(GPIOI, &gpio);
    gpio.Pin = GPIO_PIN_2;                                    /* PI: G7 */
    HAL_GPIO_Init(GPIOI, &gpio);
    gpio.Pin = GPIO_PIN_6;                                    /* PG: R7 */
    HAL_GPIO_Init(GPIOG, &gpio);
    gpio.Pin = GPIO_PIN_7;                                    /* PG: CLK */
    HAL_GPIO_Init(GPIOG, &gpio);
    gpio.Pin = GPIO_PIN_12;                                   /* PG: B1 */
    HAL_GPIO_Init(GPIOG, &gpio);
    gpio.Pin = GPIO_PIN_6;                                    /* PD: B2 */
    HAL_GPIO_Init(GPIOD, &gpio);
    gpio.Pin = GPIO_PIN_4;                                    /* PI: B4 */
    HAL_GPIO_Init(GPIOI, &gpio);
    gpio.Pin = GPIO_PIN_3;                                    /* PA: B5 */
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9;                       /* PB: B6, B7 */
    HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = GPIO_PIN_10;                                   /* PF: DE */
    HAL_GPIO_Init(GPIOF, &gpio);
    gpio.Pin = GPIO_PIN_10;                                   /* PI: HSYNC */
    HAL_GPIO_Init(GPIOI, &gpio);
    gpio.Pin = GPIO_PIN_9;                                    /* PI: VSYNC */
    HAL_GPIO_Init(GPIOI, &gpio);
    gpio.Pin = GPIO_PIN_7;                                    /* PC: G6 */
    HAL_GPIO_Init(GPIOC, &gpio);

    gpio.Alternate = GPIO_AF9_LTDC;
    gpio.Pin = GPIO_PIN_0;                                    /* PB: R3 */
    HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = GPIO_PIN_1;                                    /* PB: R6 */
    HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = GPIO_PIN_10;                                   /* PG: G3 */
    HAL_GPIO_Init(GPIOG, &gpio);

    /* LCD backlight (PD7) and LCD enable DISP (PD4): push-pull outputs
     * pulled high. Without these the panel stays dark even with LTDC running. */
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = 0U;
    gpio.Pin = GPIO_PIN_7 | GPIO_PIN_4;
    HAL_GPIO_Init(GPIOD, &gpio);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_7 | GPIO_PIN_4, GPIO_PIN_SET);
}

void LTDC_Display_Init(void)
{
    LTDC_LayerCfgTypeDef layer = {0};

#if !defined(DATA_IN_ExtSDRAM)
    /* Bare build: bring up SDRAM before any framebuffer access. */
    Board_SDRAM_EarlyInit();
#endif

    LtdcGpioInit();

    /* Pixel clock ~9 MHz: PLLSAI N=420, R=6, DIVR=8. */
    __HAL_RCC_PLLSAI_CONFIG(420U, 7U, 6U);
    RCC->DCKCFGR = (RCC->DCKCFGR & ~RCC_DCKCFGR_PLLSAIDIVR) | RCC_DCKCFGR_PLLSAIDIVR_1;
    __HAL_RCC_PLLSAI_ENABLE();
    while ((RCC->CR & RCC_CR_PLLSAIRDY) == 0U) { }

    __HAL_RCC_LTDC_CLK_ENABLE();

    hltdc.Instance = LTDC;
    hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
    hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
    hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
    hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
    hltdc.Init.HorizontalSync = LCD_HSW - 1U;
    hltdc.Init.VerticalSync = LCD_VSW - 1U;
    hltdc.Init.AccumulatedHBP = LCD_HSW + LCD_HBP - 1U;
    hltdc.Init.AccumulatedVBP = LCD_VSW + LCD_VBP - 1U;
    hltdc.Init.AccumulatedActiveW = LCD_HSW + LCD_HBP + LCD_WIDTH - 1U;
    hltdc.Init.AccumulatedActiveH = LCD_VSW + LCD_VBP + LCD_HEIGHT - 1U;
    hltdc.Init.TotalWidth = LCD_HSW + LCD_HBP + LCD_WIDTH + LCD_HFP - 1U;
    hltdc.Init.TotalHeigh = LCD_VSW + LCD_VBP + LCD_HEIGHT + LCD_VFP - 1U;
    hltdc.Init.Backcolor.Red = 0U;
    hltdc.Init.Backcolor.Green = 0U;
    hltdc.Init.Backcolor.Blue = 0U;

    if (HAL_LTDC_Init(&hltdc) != HAL_OK)
    {
        Error_Handler();
    }

    layer.WindowX0 = 0U;
    layer.WindowX1 = LCD_WIDTH - 1U;
    layer.WindowY0 = 0U;
    layer.WindowY1 = LCD_HEIGHT - 1U;
    layer.PixelFormat = LTDC_PIXEL_FORMAT_RGB888;
    layer.Alpha = 255U;
    layer.Alpha0 = 0U;
    layer.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
    layer.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
    layer.FBStartAdress = LTDC_Display_FrameBuffer();
    layer.ImageWidth = LCD_WIDTH;
    layer.ImageHeight = LCD_HEIGHT;
    layer.Backcolor.Red = 0U;
    layer.Backcolor.Green = 0U;
    layer.Backcolor.Blue = 0U;

    if (HAL_LTDC_ConfigLayer(&hltdc, &layer, LTDC_LAYER_1) != HAL_OK)
    {
        Error_Handler();
    }

    LTDC_Clear(0x000000U);
}

void LTDC_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color)
{
    if ((uint32_t)x + w > LCD_WIDTH || (uint32_t)y + h > LCD_HEIGHT)
    {
        return;
    }

    volatile uint8_t *fb = (volatile uint8_t *)(uintptr_t)FRAME_BUFFER_ADDRESS;
    uint8_t red = (uint8_t)(color >> 16);
    uint8_t green = (uint8_t)(color >> 8);
    uint8_t blue = (uint8_t)color;

    for (uint16_t row = 0; row < h; row++)
    {
        volatile uint8_t *line = &fb[((uint32_t)(y + row) * LCD_WIDTH + x) * 3U];
        for (uint16_t column = 0; column < w; column++)
        {
            *line++ = red;
            *line++ = green;
            *line++ = blue;
        }
    }
}

void LTDC_Clear(uint32_t color)
{
    LTDC_FillRect(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
}

/* ------------------------------------------------------------------ */
/* Drawing primitives (simple, unclipped where noted).                */

void LTDC_DrawLine(int x0, int y0, int x1, int y1, uint32_t color)
{
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1)
    {
        if (x0 >= 0 && x0 < (int)LCD_WIDTH && y0 >= 0 && y0 < (int)LCD_HEIGHT)
        {
            LTDC_FillRect((uint16_t)x0, (uint16_t)y0, 1, 1, color);
        }
        if (x0 == x1 && y0 == y1)
        {
            break;
        }
        int e2 = 2 * err;
        if (e2 > -dy)
        {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

void LTDC_FillCircle(int cx, int cy, int r, uint32_t color)
{
    int x0 = cx - r;
    int x1 = cx + r;
    int y0 = cy - r;
    int y1 = cy + r;
    int r2 = r * r;

    for (int y = y0; y <= y1; y++)
    {
        for (int x = x0; x <= x1; x++)
        {
            int dx = x - cx;
            int dy = y - cy;
            if (dx * dx + dy * dy <= r2)
            {
                if (x >= 0 && x < (int)LCD_WIDTH && y >= 0 && y < (int)LCD_HEIGHT)
                {
                    LTDC_FillRect((uint16_t)x, (uint16_t)y, 1, 1, color);
                }
            }
        }
    }
}

/* 5x7 ASCII font (0x20..0x7E), classic Font5x7. Each glyph is 5 bytes, one
 * byte per column, bit0 = top row. Rendered at 2x scale (10x14 px). */
static const uint8_t font5x7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* space */
    {0x00,0x00,0x5F,0x00,0x00}, /* ! */
    {0x00,0x07,0x00,0x07,0x00}, /* " */
    {0x14,0x7F,0x14,0x7F,0x14}, /* # */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* $ */
    {0x23,0x13,0x08,0x64,0x62}, /* % */
    {0x36,0x49,0x55,0x22,0x50}, /* & */
    {0x00,0x05,0x03,0x00,0x00}, /* ' */
    {0x00,0x1C,0x22,0x41,0x00}, /* ( */
    {0x00,0x41,0x22,0x1C,0x00}, /* ) */
    {0x14,0x08,0x3E,0x08,0x14}, /* * */
    {0x08,0x08,0x3E,0x08,0x08}, /* + */
    {0x00,0x50,0x30,0x00,0x00}, /* , */
    {0x08,0x08,0x08,0x08,0x08}, /* - */
    {0x00,0x60,0x60,0x00,0x00}, /* . */
    {0x20,0x10,0x08,0x04,0x02}, /* / */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */
    {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */
    {0x21,0x41,0x45,0x4B,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 4 */
    {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */
    {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */
    {0x06,0x49,0x49,0x29,0x1E}, /* 9 */
    {0x00,0x36,0x36,0x00,0x00}, /* : */
    {0x00,0x56,0x36,0x00,0x00}, /* ; */
    {0x08,0x14,0x22,0x41,0x00}, /* < */
    {0x14,0x14,0x14,0x14,0x14}, /* = */
    {0x00,0x41,0x22,0x14,0x08}, /* > */
    {0x02,0x01,0x51,0x09,0x06}, /* ? */
    {0x32,0x49,0x79,0x41,0x3E}, /* @ */
    {0x7E,0x11,0x11,0x11,0x7E}, /* A */
    {0x7F,0x49,0x49,0x49,0x36}, /* B */
    {0x3E,0x41,0x41,0x41,0x22}, /* C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* D */
    {0x7F,0x49,0x49,0x49,0x41}, /* E */
    {0x7F,0x09,0x09,0x09,0x01}, /* F */
    {0x3E,0x41,0x49,0x49,0x7A}, /* G */
    {0x7F,0x08,0x08,0x08,0x7F}, /* H */
    {0x00,0x41,0x7F,0x41,0x00}, /* I */
    {0x20,0x40,0x41,0x3F,0x01}, /* J */
    {0x7F,0x08,0x14,0x22,0x41}, /* K */
    {0x7F,0x40,0x40,0x40,0x40}, /* L */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* M */
    {0x7F,0x04,0x08,0x10,0x7F}, /* N */
    {0x3E,0x41,0x41,0x41,0x3E}, /* O */
    {0x7F,0x09,0x09,0x09,0x06}, /* P */
    {0x3E,0x41,0x51,0x21,0x5E}, /* Q */
    {0x7F,0x09,0x19,0x29,0x46}, /* R */
    {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x01,0x01,0x7F,0x01,0x01}, /* T */
    {0x3F,0x40,0x40,0x40,0x3F}, /* U */
    {0x1F,0x20,0x40,0x20,0x1F}, /* V */
    {0x3F,0x40,0x38,0x40,0x3F}, /* W */
    {0x63,0x14,0x08,0x14,0x63}, /* X */
    {0x07,0x08,0x70,0x08,0x07}, /* Y */
    {0x61,0x51,0x49,0x45,0x43}, /* Z */
    {0x00,0x7F,0x41,0x41,0x00}, /* [ */
    {0x02,0x04,0x08,0x10,0x20}, /* backslash */
    {0x00,0x41,0x41,0x7F,0x00}, /* ] */
    {0x04,0x02,0x01,0x02,0x04}, /* ^ */
    {0x40,0x40,0x40,0x40,0x40}, /* _ */
    {0x00,0x01,0x02,0x04,0x00}, /* ` */
    {0x20,0x54,0x54,0x54,0x78}, /* a */
    {0x7F,0x48,0x44,0x44,0x38}, /* b */
    {0x38,0x44,0x44,0x44,0x20}, /* c */
    {0x38,0x44,0x44,0x48,0x7F}, /* d */
    {0x38,0x54,0x54,0x54,0x18}, /* e */
    {0x08,0x7E,0x09,0x01,0x02}, /* f */
    {0x0C,0x52,0x52,0x52,0x3E}, /* g */
    {0x7F,0x08,0x04,0x04,0x78}, /* h */
    {0x00,0x44,0x7D,0x40,0x00}, /* i */
    {0x20,0x40,0x44,0x3D,0x00}, /* j */
    {0x7F,0x10,0x28,0x44,0x00}, /* k */
    {0x00,0x41,0x7F,0x40,0x00}, /* l */
    {0x7C,0x04,0x18,0x04,0x78}, /* m */
    {0x7C,0x08,0x04,0x04,0x78}, /* n */
    {0x38,0x44,0x44,0x44,0x38}, /* o */
    {0x7C,0x14,0x14,0x14,0x08}, /* p */
    {0x08,0x14,0x14,0x18,0x7C}, /* q */
    {0x7C,0x08,0x04,0x04,0x08}, /* r */
    {0x48,0x54,0x54,0x54,0x20}, /* s */
    {0x04,0x3F,0x44,0x40,0x20}, /* t */
    {0x3C,0x40,0x40,0x20,0x7C}, /* u */
    {0x1C,0x20,0x40,0x20,0x1C}, /* v */
    {0x3C,0x40,0x30,0x40,0x3C}, /* w */
    {0x44,0x28,0x10,0x28,0x44}, /* x */
    {0x0C,0x50,0x50,0x50,0x3C}, /* y */
    {0x44,0x64,0x54,0x4C,0x44}, /* z */
    {0x00,0x08,0x36,0x41,0x00}, /* { */
    {0x00,0x00,0x7F,0x00,0x00}, /* | */
    {0x00,0x41,0x36,0x08,0x00}, /* } */
    {0x08,0x08,0x2A,0x1C,0x08}, /* ~ */
};

void LTDC_DrawString(uint16_t x, uint16_t y, const char *str,
                     uint32_t fg_color, uint32_t bg_color)
{
    while (*str != '\0')
    {
        char c = *str;
        if (c >= ' ' && c <= '~')
        {
            const uint8_t *glyph = font5x7[c - ' '];
            for (int col = 0; col < 5; col++)
            {
                uint8_t bits = glyph[col];
                for (int row = 0; row < 7; row++)
                {
                    uint32_t color = (bits & (1U << row)) ? fg_color : bg_color;
                    LTDC_FillRect((uint16_t)(x + col * 2), (uint16_t)(y + row * 2),
                                  2, 2, color);
                }
            }
            x += 12;   /* 5x7 at 2x scale + 2px spacing */
        }
        else if (c == '\n')
        {
            break;
        }
        str++;
    }
}

void Touch_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* SCL/SDA: open-drain outputs with pull-ups (bit-banged I2C). */
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Pin = TOUCH_SCL_PIN | TOUCH_SDA_PIN;
    HAL_GPIO_Init(GPIOH, &gpio);
    TouchSDA(1);
    TouchSCL(1);

    /* RST and INT: push-pull outputs during the reset sequence. */
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    gpio.Pin = TOUCH_RST_PIN;
    HAL_GPIO_Init(TOUCH_RST_PORT, &gpio);
    gpio.Pin = TOUCH_INT_PIN;
    HAL_GPIO_Init(TOUCH_INT_PORT, &gpio);

    /* GT9xx reset: RST low, then high. The INT level during reset selects
     * the I2C address; INT low -> address 0x28/0x29 (7-bit 0x14). */
    HAL_GPIO_WritePin(TOUCH_INT_PORT, TOUCH_INT_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TOUCH_RST_PORT, TOUCH_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(20U);
    HAL_GPIO_WritePin(TOUCH_RST_PORT, TOUCH_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(50U);

    /* INT back to floating input (touch ready line). */
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    gpio.Pin = TOUCH_INT_PIN;
    HAL_GPIO_Init(TOUCH_INT_PORT, &gpio);
}

/* Reads the touch controller product ID/version (reg 0x8140, 4 bytes).
 * Returns 1 if the IC answers, 0 otherwise. */
int Touch_ReadVersion(uint8_t version[4])
{
    return TouchI2CReadReg16(TOUCH_REG_VERSION, version, 4U) == 0;
}

/* GT911 configuration for the 5-inch 800x480 panel (vendor CTP_CFG_GT911).
 * Bytes 1..4 already carry X_MAX=800, Y_MAX=480. */
#define GT911_CFG_REG   0x8047U
#define GT911_CFG_LEN   186U
static const uint8_t gt911_config[GT911_CFG_LEN] = {
    0x00,0x20,0x03,0xE0,0x01,0x05,0x0D,0x00,0x01,0x08,
    0x28,0x0F,0x50,0x32,0x03,0x05,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x8A,0x2A,0x0C,
    0x45,0x47,0x0C,0x08,0x00,0x00,0x00,0x02,0x02,0x2D,
    0x00,0x00,0x00,0x00,0x00,0x03,0x64,0x32,0x00,0x00,
    0x00,0x28,0x64,0x94,0xC5,0x02,0x07,0x00,0x00,0x04,
    0x9C,0x2C,0x00,0x8F,0x34,0x00,0x84,0x3F,0x00,0x7C,
    0x4C,0x00,0x77,0x5B,0x00,0x77,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x18,0x16,0x14,0x12,0x10,0x0E,0x0C,0x0A,
    0x08,0x06,0x04,0x02,0xFF,0xFF,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x16,0x18,0x1C,0x1D,0x1E,0x1F,0x20,0x21,
    0x22,0x24,0x13,0x12,0x10,0x0F,0x0A,0x08,0x06,0x04,
    0x02,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x24,0x01
};

/* Uploads the GT911 800x480 config and requests the chip to apply it.
 * Replicates the vendor sequence: 186 config bytes, then a checksum byte
 * (two's complement of the first 184 bytes) and a refresh flag byte. */
int Touch_LoadConfig(void)
{
    uint8_t buffer[GT911_CFG_LEN + 2];
    uint16_t checksum = 0;

    for (uint16_t i = 0; i < GT911_CFG_LEN; i++)
    {
        buffer[i] = gt911_config[i];
    }
    for (uint16_t i = 0; i < GT911_CFG_LEN - 2; i++)
    {
        checksum += buffer[i];
    }
    buffer[GT911_CFG_LEN]     = (uint8_t)((~(checksum & 0xFFU)) + 1U);
    buffer[GT911_CFG_LEN + 1] = 1U;   /* refresh flag */

    int rc = TouchI2CWriteBuffer(GT911_CFG_REG, buffer, GT911_CFG_LEN + 2U);
    HAL_Delay(10U);
    return rc;
}

/* Diagnostic: reset the controller with INT low or high, then probe candidate
 * 8-bit addresses with a dummy write. Returns the address that ACKs, or -1. */
int Touch_Probe(int int_high)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = TOUCH_INT_PIN;
    HAL_GPIO_Init(TOUCH_INT_PORT, &gpio);

    HAL_GPIO_WritePin(TOUCH_INT_PORT, TOUCH_INT_PIN,
                      int_high ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TOUCH_RST_PORT, TOUCH_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(20U);
    HAL_GPIO_WritePin(TOUCH_RST_PORT, TOUCH_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(50U);

    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    gpio.Pin = TOUCH_INT_PIN;
    HAL_GPIO_Init(TOUCH_INT_PORT, &gpio);

    const uint8_t candidates[] = {0x28U, 0xBAU, 0x50U, 0x29U, 0x5DU, 0x51U};
    for (unsigned i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
    {
        TouchI2CStart();
        int nack = TouchI2CWriteByte(candidates[i]);
        TouchI2CStop();
        if (nack == 0)
        {
            return candidates[i];
        }
    }
    return -1;
}

TouchPoint Touch_Scan(void)
{
    TouchPoint point = {0, 0, 0};
    uint8_t status = 0;
    uint8_t data[8] = {0};

    if (TouchI2CReadReg16(TOUCH_REG_STATUS, &status, 1U) != 0)
    {
        return point;
    }

    if ((status & 0x80U) == 0U)
    {
        return point;
    }

    if (TouchI2CReadReg16(TOUCH_REG_DATA, data, sizeof(data)) != 0)
    {
        return point;
    }

    /* Clear the status register's buffer-ready bit. */
    (void)TouchI2CWriteReg16(TOUCH_REG_STATUS, 0U);

    point.pressed = status & 0x0FU;
    if (data[0] & 0x80U)   /* point 1 valid flag */
    {
        point.x = (uint16_t)(((uint16_t)data[1] << 8) | data[2]);
        point.y = (uint16_t)(((uint16_t)data[3] << 8) | data[4]);
    }

    if (point.x >= LCD_WIDTH)
    {
        point.x = LCD_WIDTH - 1U;
    }
    if (point.y >= LCD_HEIGHT)
    {
        point.y = LCD_HEIGHT - 1U;
    }
    return point;
}
