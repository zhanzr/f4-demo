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

/* GT1151QM touch on I2C2. */
#define TOUCH_I2C_ADDR   (0x28U)
#define TOUCH_REG_STATUS 0x814EU
#define TOUCH_REG_DATA   0x814FU
#define TOUCH_RST_PORT   GPIOD
#define TOUCH_RST_PIN    GPIO_PIN_11
#define TOUCH_TIMEOUT    100U

static LTDC_HandleTypeDef hltdc;
static I2C_HandleTypeDef htouch_i2c;

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

void Touch_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_I2C2_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_4 | GPIO_PIN_5;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF4_I2C2;
    HAL_GPIO_Init(GPIOH, &gpio);

    gpio.Pin = TOUCH_RST_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(TOUCH_RST_PORT, &gpio);

    HAL_GPIO_WritePin(TOUCH_RST_PORT, TOUCH_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(10U);
    HAL_GPIO_WritePin(TOUCH_RST_PORT, TOUCH_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(1U);
    HAL_GPIO_WritePin(TOUCH_RST_PORT, TOUCH_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(10U);

    htouch_i2c.Instance = I2C2;
    htouch_i2c.Init.ClockSpeed = 400000U;
    htouch_i2c.Init.DutyCycle = I2C_DUTYCYCLE_2;
    htouch_i2c.Init.OwnAddress1 = 0U;
    htouch_i2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    htouch_i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    htouch_i2c.Init.OwnAddress2 = 0U;
    htouch_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    htouch_i2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&htouch_i2c) != HAL_OK)
    {
        Error_Handler();
    }
}

TouchPoint Touch_Scan(void)
{
    TouchPoint point = {0, 0, 0};
    uint8_t status = 0;
    uint8_t data[6] = {0};

    if (HAL_I2C_Mem_Read(&htouch_i2c, TOUCH_I2C_ADDR << 1, TOUCH_REG_STATUS,
                         I2C_MEMADD_SIZE_16BIT, &status, 1U, TOUCH_TIMEOUT) != HAL_OK)
    {
        return point;
    }

    if ((status & 0x80U) == 0U)
    {
        return point;
    }

    if (HAL_I2C_Mem_Read(&htouch_i2c, TOUCH_I2C_ADDR << 1, TOUCH_REG_DATA,
                         I2C_MEMADD_SIZE_16BIT, data, 6U, TOUCH_TIMEOUT) != HAL_OK)
    {
        return point;
    }

    /* Clear the status register's buffer-ready bit. */
    uint8_t clear = 0U;
    (void)HAL_I2C_Mem_Write(&htouch_i2c, TOUCH_I2C_ADDR << 1, TOUCH_REG_STATUS,
                            I2C_MEMADD_SIZE_16BIT, &clear, 1U, TOUCH_TIMEOUT);

    point.pressed = status & 0x0FU;
    point.x = (uint16_t)(((uint16_t)data[1] << 8) | data[2]);
    point.y = (uint16_t)(((uint16_t)data[3] << 8) | data[4]);

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
