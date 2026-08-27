/**
  * @file    ov7670_to_lcd/src/lcd_camera.c
  * @brief   Two-layer LTDC driver (see lcd_camera.h), built directly on the
  *          vendored STM32F4 HAL (no copied vendor-example drivers).
  *
  * Panel timing, LTDC pin mux and pixel clock are identical to
  * board/board_ltdc.c (the proven fire-f429 config). SDRAM uses
  * Board_SDRAM_EarlyInit() (8-col/12-row/CAS2, read-burst ON).
  */

#include "lcd_camera.h"
#include "board.h"
#include "stm32f4xx_hal_ltdc.h"

/* board/board_sdram.c - shared board SDRAM init (not declared in board.h). */
extern void Board_SDRAM_EarlyInit(void);

/* 5-inch 800x480 panel timing (vendor example / board_ltdc.c). */
#define LCD_HBP 46U
#define LCD_VBP 23U
#define LCD_HSW 1U
#define LCD_VSW 3U
#define LCD_HFP 40U
#define LCD_VFP 13U

static LTDC_HandleTypeDef hltdc;

/* ------------------------------------------------------------------ */
/* LTDC pin muxing (identical to board/board_ltdc.c).                  */

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
    gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_8;
    HAL_GPIO_Init(GPIOH, &gpio);
    gpio.Pin = GPIO_PIN_13 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOH, &gpio);
    gpio.Pin = GPIO_PIN_11 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_6;
    HAL_GPIO_Init(GPIOE, &gpio);
    gpio.Pin = GPIO_PIN_4;
    HAL_GPIO_Init(GPIOE, &gpio);
    gpio.Pin = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOI, &gpio);
    gpio.Pin = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOI, &gpio);
    gpio.Pin = GPIO_PIN_6;
    HAL_GPIO_Init(GPIOG, &gpio);
    gpio.Pin = GPIO_PIN_7;
    HAL_GPIO_Init(GPIOG, &gpio);
    gpio.Pin = GPIO_PIN_12;
    HAL_GPIO_Init(GPIOG, &gpio);
    gpio.Pin = GPIO_PIN_6;
    HAL_GPIO_Init(GPIOD, &gpio);
    gpio.Pin = GPIO_PIN_4;
    HAL_GPIO_Init(GPIOI, &gpio);
    gpio.Pin = GPIO_PIN_3;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = GPIO_PIN_10;
    HAL_GPIO_Init(GPIOF, &gpio);
    gpio.Pin = GPIO_PIN_10;
    HAL_GPIO_Init(GPIOI, &gpio);
    gpio.Pin = GPIO_PIN_9;
    HAL_GPIO_Init(GPIOI, &gpio);
    gpio.Pin = GPIO_PIN_7;
    HAL_GPIO_Init(GPIOC, &gpio);

    gpio.Alternate = GPIO_AF9_LTDC;
    gpio.Pin = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = GPIO_PIN_1;
    HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = GPIO_PIN_10;
    HAL_GPIO_Init(GPIOG, &gpio);

    /* LCD backlight (PD7) + LCD enable DISP (PD4): push-pull, high. */
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = 0U;
    gpio.Pin = GPIO_PIN_7 | GPIO_PIN_4;
    HAL_GPIO_Init(GPIOD, &gpio);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_7 | GPIO_PIN_4, GPIO_PIN_SET);
}

/* ------------------------------------------------------------------ */

void LcdCamera_Init(void)
{
    LTDC_LayerCfgTypeDef layer = {0};

    /* SDRAM for the framebuffers (board-proven config: 8/12, CAS2,
     * read-burst ON - required to feed the LTDC FIFO without smearing). */
    Board_SDRAM_EarlyInit();
    LtdcGpioInit();

    /* Pixel clock 8.75 MHz: PLLSAI N=420, R=6, DIVR=8 (board_ltdc.c). */
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

    /* Layer 1: camera, RGB565, full screen. */
    layer.WindowX0 = 0U;
    layer.WindowX1 = LCD_WIDTH - 1U;
    layer.WindowY0 = 0U;
    layer.WindowY1 = LCD_HEIGHT - 1U;
    layer.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
    layer.Alpha = 255U;
    layer.Alpha0 = 0U;
    layer.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
    layer.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
    layer.FBStartAdress = LCD_FB_CAM0;
    layer.ImageWidth = LCD_WIDTH;
    layer.ImageHeight = LCD_HEIGHT;
    layer.Backcolor.Red = 0U;
    layer.Backcolor.Green = 0U;
    layer.Backcolor.Blue = 0U;
    if (HAL_LTDC_ConfigLayer(&hltdc, &layer, LTDC_LAYER_1) != HAL_OK)
    {
        Error_Handler();
    }

    /* Layer 2: text overlay, ARGB8888, alpha 0x00 = transparent. */
    layer.PixelFormat = LTDC_PIXEL_FORMAT_ARGB8888;
    layer.Alpha = 255U;
    layer.FBStartAdress = LCD_FB_TEXT;
    if (HAL_LTDC_ConfigLayer(&hltdc, &layer, LTDC_LAYER_2) != HAL_OK)
    {
        Error_Handler();
    }

    /* Clean start: transparent overlay, black camera buffers. */
    LcdCamera_TextClear(0, 0, LCD_WIDTH, LCD_HEIGHT);
    LcdCamera_CameraFill(LCD_FB_CAM0, 0, 0, LCD_WIDTH, LCD_HEIGHT, 0x0000U);
    LcdCamera_CameraFill(LCD_FB_CAM1, 0, 0, LCD_WIDTH, LCD_HEIGHT, 0x0000U);
}

/* ------------------------------------------------------------------ */

void LcdCamera_SetLayer0FB(uint32_t addr)
{
    (void)HAL_LTDC_SetAddress_NoReload(&hltdc, addr, LTDC_LAYER_1);
    (void)HAL_LTDC_Reload(&hltdc, LTDC_RELOAD_VERTICAL_BLANKING);
}

void LcdCamera_CameraFill(uint32_t fb, uint16_t x, uint16_t y, uint16_t w,
                          uint16_t h, uint16_t rgb565)
{
    volatile uint16_t *p = (volatile uint16_t *)(uintptr_t)fb;
    if ((uint32_t)x + w > LCD_WIDTH)  w = (uint16_t)(LCD_WIDTH - x);
    if ((uint32_t)y + h > LCD_HEIGHT) h = (uint16_t)(LCD_HEIGHT - y);
    for (uint32_t row = 0; row < h; row++)
    {
        volatile uint16_t *line = &p[(uint32_t)(y + row) * LCD_WIDTH + x];
        for (uint32_t col = 0; col < w; col++)
        {
            line[col] = rgb565;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Text overlay: 5x7 ASCII font, ARGB8888 pixels.                     */

static const uint8_t font5x7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00}, {0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7F,0x14,0x7F,0x14}, {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50}, {0x00,0x05,0x03,0x00,0x00}, {0x00,0x1C,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1C,0x00}, {0x14,0x08,0x3E,0x08,0x14}, {0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08}, {0x00,0x60,0x60,0x00,0x00},
    {0x20,0x10,0x08,0x04,0x02}, {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31}, {0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}, {0x00,0x36,0x36,0x00,0x00},
    {0x00,0x56,0x36,0x00,0x00}, {0x08,0x14,0x22,0x41,0x00}, {0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08}, {0x02,0x01,0x51,0x09,0x06}, {0x32,0x49,0x79,0x41,0x3E},
    {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}, {0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20}, {0x00,0x41,0x41,0x7F,0x00}, {0x04,0x02,0x01,0x02,0x04},
    {0x40,0x40,0x40,0x40,0x40}, {0x00,0x01,0x02,0x04,0x00}, {0x20,0x54,0x54,0x54,0x78},
    {0x7F,0x48,0x44,0x44,0x38}, {0x38,0x44,0x44,0x44,0x20}, {0x38,0x44,0x44,0x48,0x7F},
    {0x38,0x54,0x54,0x54,0x18}, {0x08,0x7E,0x09,0x01,0x02}, {0x0C,0x52,0x52,0x52,0x3E},
    {0x7F,0x08,0x04,0x04,0x78}, {0x00,0x44,0x7D,0x40,0x00}, {0x20,0x40,0x44,0x3D,0x00},
    {0x7F,0x10,0x28,0x44,0x00}, {0x00,0x41,0x7F,0x40,0x00}, {0x7C,0x04,0x18,0x04,0x78},
    {0x7C,0x08,0x04,0x04,0x78}, {0x38,0x44,0x44,0x44,0x38}, {0x7C,0x14,0x14,0x14,0x08},
    {0x08,0x14,0x14,0x18,0x7C}, {0x7C,0x08,0x04,0x04,0x08}, {0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20}, {0x3C,0x40,0x40,0x20,0x7C}, {0x1C,0x20,0x40,0x20,0x1C},
    {0x3C,0x40,0x30,0x40,0x3C}, {0x44,0x28,0x10,0x28,0x44}, {0x0C,0x50,0x50,0x50,0x3C},
    {0x44,0x64,0x54,0x4C,0x44}, {0x00,0x08,0x36,0x41,0x00}, {0x00,0x00,0x7F,0x00,0x00},
    {0x00,0x41,0x36,0x08,0x00}, {0x08,0x08,0x2A,0x1C,0x08},
};

void LcdCamera_TextClear(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    if ((uint32_t)x + w > LCD_WIDTH)  w = (uint16_t)(LCD_WIDTH - x);
    if ((uint32_t)y + h > LCD_HEIGHT) h = (uint16_t)(LCD_HEIGHT - y);

    volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)LCD_FB_TEXT;
    for (uint32_t row = 0; row < h; row++)
    {
        volatile uint32_t *line = &fb[(uint32_t)(y + row) * LCD_WIDTH + x];
        for (uint32_t col = 0; col < w; col++)
        {
            line[col] = 0x00000000U;   /* fully transparent */
        }
    }
}

void LcdCamera_AsciiString(uint16_t x, uint16_t y, const char *str,
                           uint32_t fg, uint32_t bg, uint8_t scale)
{
    volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)LCD_FB_TEXT;
    uint32_t sc = (scale == 0U) ? 2U : scale;
    uint16_t cx = x;

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
                    uint32_t color = (bits & (1U << row)) ? fg : bg;
                    for (uint32_t dy = 0; dy < sc; dy++)
                    {
                        for (uint32_t dx = 0; dx < sc; dx++)
                        {
                            uint32_t px = (uint32_t)(y + row * sc + dy) * LCD_WIDTH
                                        + (uint32_t)(cx + col * sc + dx);
                            if (px < (uint32_t)(LCD_WIDTH * LCD_HEIGHT))
                            {
                                fb[px] = color;
                            }
                        }
                    }
                }
            }
            cx = (uint16_t)(cx + 6U * sc);
        }
        str++;
    }
}