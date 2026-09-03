/*
 * ST7735S 1.44" 128x128 TFT driver for the nano-f407 board (STM32F407VET6).
 * Uses bit-banged (soft) SPI, following the proven st7735_softSPI driver
 * from the nucleo-f042k6 repo: full software control of MOSI/SCL eliminates
 * hardware-SPI clock/phase/polarity marginality (which produced random
 * "Z-shape" garbage on hardware SPI).
 *
 *   LCD_SCL   -> PB13  (GPIO out, soft SCL)
 *   LCD_MOSI  -> PB15  (GPIO out, soft MOSI)
 *   LCD_CS    -> PB12  (GPIO out, software CS)
 *   LCD_DC    -> PC6   (GPIO out, register/data)
 *   LCD_RST   -> PB1   (GPIO out, reset)
 *   LCD_BL    -> PE9   (TIM1_CH1 PWM, 8% to limit backlight current)
 *
 * Init sequence follows the vendor STM32F103R / hard_spi example (ST7735S,
 * 128x128, portrait: column offset 2, row offset 3, MADCTL 0xC8).
 */

#include "st7735s.h"
#include "board.h"
#include "stm32f4xx_hal.h"

#define LCD_PORT_SPI   GPIOB
#define LCD_SCK_PIN    GPIO_PIN_13   /* soft SCL */
#define LCD_MOSI_PIN   GPIO_PIN_15   /* soft MOSI */
#define LCD_CS_PIN     GPIO_PIN_12   /* CS (GPIO) */

#define LCD_DC_PORT    GPIOC
#define LCD_DC_PIN     GPIO_PIN_6
#define LCD_RST_PORT   GPIOB
#define LCD_RST_PIN    GPIO_PIN_1

#define LCD_BL_PORT    GPIOE
#define LCD_BL_PIN     GPIO_PIN_9   /* TIM1_CH1 (AF1) */

#define LCD_CS_HIGH()  HAL_GPIO_WritePin(LCD_PORT_SPI, LCD_CS_PIN, GPIO_PIN_SET)
#define LCD_CS_LOW()   HAL_GPIO_WritePin(LCD_PORT_SPI, LCD_CS_PIN, GPIO_PIN_RESET)
#define LCD_DC_HIGH()  HAL_GPIO_WritePin(LCD_DC_PORT, LCD_DC_PIN, GPIO_PIN_SET)
#define LCD_DC_LOW()   HAL_GPIO_WritePin(LCD_DC_PORT, LCD_DC_PIN, GPIO_PIN_RESET)
#define LCD_RST_HIGH() HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_SET)
#define LCD_RST_LOW()  HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_RESET)
#define LCD_SCL_HIGH() HAL_GPIO_WritePin(LCD_PORT_SPI, LCD_SCK_PIN, GPIO_PIN_SET)
#define LCD_SCL_LOW()  HAL_GPIO_WritePin(LCD_PORT_SPI, LCD_SCK_PIN, GPIO_PIN_RESET)
#define LCD_MOSI_HIGH() HAL_GPIO_WritePin(LCD_PORT_SPI, LCD_MOSI_PIN, GPIO_PIN_SET)
#define LCD_MOSI_LOW()  HAL_GPIO_WritePin(LCD_PORT_SPI, LCD_MOSI_PIN, GPIO_PIN_RESET)

/* Column/row offsets to map the 128x128 active area onto the 132x162 GRAM. */
#define LCD_COL_OFFSET  2u
#define LCD_ROW_OFFSET  3u

static void lcd_gpio_init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* SCL, MOSI, CS as plain push-pull outputs (soft SPI). */
    g.Pin   = LCD_SCK_PIN | LCD_MOSI_PIN | LCD_CS_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(LCD_PORT_SPI, &g);

    /* DC and RST as plain outputs. */
    g.Pin  = LCD_DC_PIN;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(LCD_DC_PORT, &g);
    g.Pin = LCD_RST_PIN;
    HAL_GPIO_Init(LCD_RST_PORT, &g);

    LCD_CS_HIGH();
    LCD_DC_HIGH();
    LCD_RST_HIGH();
    LCD_SCL_LOW();
    LCD_MOSI_LOW();
}

/* Bit-banged SPI send, mode 0: data changes while SCL is low, the panel
 * samples on the rising edge of SCL (same as the proven st7735_softSPI). */
static void softspi_write_8bit(uint8_t dat)
{
    for (uint8_t i = 0; i < 8; i++)
    {
        if ((dat & 0x80u) != 0) { LCD_MOSI_HIGH(); }
        else                   { LCD_MOSI_LOW(); }
        dat <<= 1;
        LCD_SCL_LOW();
        LCD_SCL_HIGH();
    }
}

/* Drive the backlight via TIM1_CH1 on PE9 at 25% duty. Direct BL->VCC draws
 * too much current and browns out the 3.3V rail (which caused the display
 * noise/partial draws); a low PWM duty limits that current much like
 * removing the BL load did. */
void ST7735_Backlight_Init(void)
{
    GPIO_InitTypeDef g = {0};
    TIM_OC_InitTypeDef oc = {0};
    TIM_HandleTypeDef htim = {0};

    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_TIM1_CLK_ENABLE();

    g.Pin       = LCD_BL_PIN;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF1_TIM1;
    HAL_GPIO_Init(LCD_BL_PORT, &g);

    /* TIM1CLK = 24 MHz (board now runs at 24 MHz, APB2 = /1 -> PCLK2=24 MHz,
     * timer prescaler /1 so no x2). PSC=23 -> 1 MHz tick, ARR=999 -> 1 kHz
     * PWM. */
    htim.Instance               = TIM1;
    htim.Init.Prescaler         = 23;
    htim.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim.Init.Period            = 999;
    htim.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim.Init.RepetitionCounter = 0;
    htim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&htim) != HAL_OK)
    {
        Error_Handler();
    }

    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.Pulse      = 80;               /* 8% of ARR+1 = 1000 */
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim, &oc, TIM_CHANNEL_1) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_TIM_PWM_Start(&htim, TIM_CHANNEL_1) != HAL_OK)
    {
        Error_Handler();
    }
}

static void spi_send(const uint8_t *buf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
    {
        softspi_write_8bit(buf[i]);
    }
}

/* Small delay to guarantee a clean CS-deselect gap between serial
 * transactions. Without it, back-to-back writes can be seen by the panel as
 * one continuous stream, dropping byte framing and desyncing the controller
 * (random / partial garbage). At 24 MHz, 64 NOPs is a few microseconds. */
static void deselect_wait(void)
{
    for (volatile int i = 0; i < 64; i++) { __NOP(); }
}

static void write_cmd(uint8_t cmd)
{
    LCD_CS_LOW();
    LCD_DC_LOW();
    spi_send(&cmd, 1);
    LCD_CS_HIGH();
    deselect_wait();
}

static void write_data(uint8_t data)
{
    LCD_CS_LOW();
    LCD_DC_HIGH();
    spi_send(&data, 1);
    LCD_CS_HIGH();
    deselect_wait();
}

static void write_data_n(const uint8_t *data, uint16_t len)
{
    LCD_CS_LOW();
    LCD_DC_HIGH();
    spi_send(data, len);
    LCD_CS_HIGH();
    deselect_wait();
}

static void set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t d[4];

    write_cmd(0x2A);                       /* column address set */
    d[0] = (uint8_t)((x0 + LCD_COL_OFFSET) >> 8);
    d[1] = (uint8_t)(x0 + LCD_COL_OFFSET);
    d[2] = (uint8_t)((x1 + LCD_COL_OFFSET) >> 8);
    d[3] = (uint8_t)(x1 + LCD_COL_OFFSET);
    write_data_n(d, 4);

    write_cmd(0x2B);                       /* row address set */
    d[0] = (uint8_t)((y0 + LCD_ROW_OFFSET) >> 8);
    d[1] = (uint8_t)(y0 + LCD_ROW_OFFSET);
    d[2] = (uint8_t)((y1 + LCD_ROW_OFFSET) >> 8);
    d[3] = (uint8_t)(y1 + LCD_ROW_OFFSET);
    write_data_n(d, 4);

    write_cmd(0x2C);                       /* write to GRAM */
}

void ST7735_Init(void)
{
    lcd_gpio_init();

    /* Robust hardware reset: hold LCD in reset long, release, then let the
     * panel settle before the initialization sequence. */
    LCD_RST_LOW();
    HAL_Delay(120);
    LCD_RST_HIGH();
    HAL_Delay(200);

    /* ---- ST7735S init sequence ----
     * Power / frame-rate registers match both the vendor MD144 hard_spi
     * example and the proven nucleo-f042k6 st7735_softSPI driver. From the
     * proven softSPI driver we also take INVOFF (0x20), NORON (0x13) and the
     * CubeFW gamma tables. MADCTL/offsets stay our panel's (portrait 128x128).
     */
    write_cmd(0x11);                       /* sleep out */
    HAL_Delay(150);
    write_cmd(0x20);                       /* inversion off (proven) */
    write_cmd(0x13);                       /* normal display on (proven) */

    write_cmd(0xB1);                       /* frame rate control (normal) */
    write_data(0x01); write_data(0x2C); write_data(0x2D);
    write_cmd(0xB2);                       /* idle mode */
    write_data(0x01); write_data(0x2C); write_data(0x2D);
    write_cmd(0xB3);                       /* partial mode */
    write_data(0x01); write_data(0x2C); write_data(0x2D);
    write_data(0x01); write_data(0x2C); write_data(0x2D);
    write_cmd(0xB4);                       /* column inversion */
    write_data(0x07);

    write_cmd(0xC0);                       /* power control 1 */
    write_data(0xA2); write_data(0x02); write_data(0x84);
    write_cmd(0xC1);                       /* power control 2 */
    write_data(0xC5);
    write_cmd(0xC2);                       /* power control 3 (VCOM) */
    write_data(0x0A); write_data(0x00);
    write_cmd(0xC3);                       /* power control 4 */
    write_data(0x8A); write_data(0x2A);
    write_cmd(0xC4);                       /* power control 5 */
    write_data(0x8A); write_data(0xEE);
    write_cmd(0xC5);                       /* VCOM control 1 */
    write_data(0x0E);

    write_cmd(0x36);                       /* MADCTL: MX|MY|RGB (our panel) */
    write_data(0xC8);

    write_cmd(0xE0);                       /* gamma '+polarity' (proven) */
    { static const uint8_t g0[16] = {
        0x02,0x1c,0x07,0x12,0x37,0x32,0x29,0x2d,
        0x29,0x25,0x2B,0x39,0x00,0x01,0x03,0x10 };
        write_data_n(g0, 16);
    }
    write_cmd(0xE1);                       /* gamma '-polarity' (proven) */
    { static const uint8_t g1[16] = {
        0x03,0x1d,0x07,0x06,0x2E,0x2C,0x29,0x2D,
        0x2E,0x2E,0x37,0x3F,0x00,0x00,0x02,0x10 };
        write_data_n(g1, 16);
    }

    write_cmd(0x2A);                       /* column address set 0..127 */
    write_data(0x00); write_data(0x00); write_data(0x00); write_data(0x7F);
    write_cmd(0x2B);                       /* row address set 0..159 */
    write_data(0x00); write_data(0x00); write_data(0x00); write_data(0x9F);

    write_cmd(0xF0);                       /* enable test command */
    write_data(0x01);
    write_cmd(0xF6);                       /* disable RAM power save */
    write_data(0x00);

    write_cmd(0x3A);                       /* 16-bit (RGB565) color format */
    write_data(0x05);
    write_cmd(0x29);                       /* display on */
    HAL_Delay(100);

    ST7735_Fill(ST7735_BLACK);
}

void ST7735_Fill(uint16_t color)
{
    set_addr_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);

    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFFu);
    const uint32_t total = (uint32_t)LCD_WIDTH * LCD_HEIGHT;

    /* Write the frame in small CS-bounded chunks. The controller keeps the
     * address window and auto-increments the GRAM cursor, so re-asserting CS
     * between chunks just resumes at the current position. This resyncs the
     * serial framing frequently, so a single-bit glitch can only corrupt a
     * small chunk instead of desyncing the whole 32KB burst to garbage. */
    uint32_t done = 0;
    while (done < total)
    {
        LCD_CS_LOW();
        LCD_DC_HIGH();
        uint32_t n = 0;
        while (n < 16u && done < total)   /* 16 pixels (=32 bytes) per CS hold */
        {
            spi_send(&hi, 1);
            spi_send(&lo, 1);
            n++;
            done++;
        }
        LCD_CS_HIGH();
    }
}

void ST7735_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    set_addr_window(x, y, x, y);
    uint8_t d[2] = { (uint8_t)(color >> 8), (uint8_t)(color & 0xFFu) };
    write_data_n(d, 2);
}

void ST7735_FillRect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    if (x0 >= LCD_WIDTH || x1 >= LCD_WIDTH) return;
    if (y0 >= LCD_HEIGHT || y1 >= LCD_HEIGHT) return;
    if (x0 > x1 || y0 > y1) return;

    uint32_t w = (uint32_t)(x1 - x0 + 1);
    uint32_t h = (uint32_t)(y1 - y0 + 1);

    set_addr_window(x0, y0, x1, y1);

    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFFu);
    uint32_t total = w * h;
    uint32_t done = 0;

    /* Chunked, CS-resynced write (see ST7735_Fill note). */
    while (done < total)
    {
        LCD_CS_LOW();
        LCD_DC_HIGH();
        uint32_t n = 0;
        while (n < 16u && done < total)   /* 16 pixels (=32 bytes) per CS hold */
        {
            spi_send(&hi, 1);
            spi_send(&lo, 1);
            n++;
            done++;
        }
        LCD_CS_HIGH();
    }
}

void ST7735_BlitFB(const uint16_t *fb)
{
    set_addr_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);

    const uint32_t total = (uint32_t)LCD_WIDTH * LCD_HEIGHT;
    uint32_t done = 0;

    /* Same chunked, CS-resynced write as ST7735_Fill (see note there). */
    while (done < total)
    {
        uint8_t buf[32];
        uint16_t n = 0;
        while (n < 16u && done < total)   /* 16 pixels (=32 bytes) per CS hold */
        {
            uint16_t c = fb[done++];
            buf[(uint16_t)(n * 2u)]      = (uint8_t)(c >> 8);
            buf[(uint16_t)(n * 2u + 1u)] = (uint8_t)(c & 0xFFu);
            n++;
        }
        LCD_CS_LOW();
        LCD_DC_HIGH();
        spi_send(buf, (uint16_t)(n * 2u));
        LCD_CS_HIGH();
    }
}
