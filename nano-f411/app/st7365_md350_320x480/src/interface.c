/*
  interface.c - low-level ST7365P bus primitives (nano-f411 port).
  One wiring (SCL=PA5, SDA=PA7), two drive methods selected at runtime:

    SOFT: PA5/PA7 re-muxed to plain GPIO outputs and bit-banged (DC
          selects command vs data, CS asserted per transfer).
    HW  : PA5/PA7 re-muxed to SPI1 AF5 (SCK/MOSI), master, simplex TX
          (1LINE), 8-bit, mode 3 (CPOL=1, CPHA=1 - the same idle-high /
          rising-edge-sampling timing the bit-bang produces), MSB first,
          NSS soft. DC=PA4 / RES=PA3 / CS=PB8 stay GPIO.
          APB2 = 100 MHz (project clock override): default prescaler
          /2 = 50 MHz SCK (the F411 SPI1 max); /4 = 25 MHz available via
          LCD_SPI1_PRESC.

  Raster bursts in HW mode stream through a 512-byte TX buffer (one
  HAL_SPI_Transmit per <=512 bytes); in SOFT mode bytes go straight to the
  bit-bang.  CS is asserted per transfer, DC selects command vs data.

    SCL = PA5, SDA = PA7, DC = PA4, RES = PA3, CS = PB8 (GPIO).
    MISO = PA6 is present on the module but not used by this TX-only
    driver.
*/

#include "interface.h"
#include <stdio.h>
#include "lcd.h"
#include "stm32f4xx_hal.h"

/* Bytes of pixel data accumulated before one HAL_SPI_Transmit call (HW). */
#define SPI_TX_BUF_SIZE 512U

/* SPI1 baud prescaler: APB2 = 100 MHz (project clock override). The
 * default /2 = 50 MHz SCK is the F411 SPI1 max; /4 = 25 MHz via
 * LCD_SPI1_PRESC if a module needs a slower rate. */
#ifndef LCD_SPI1_PRESC
#define LCD_SPI1_PRESC SPI_BAUDRATEPRESCALER_2
#endif

static uint8_t           s_tx_buf[SPI_TX_BUF_SIZE];
static uint16_t          s_tx_len;
static SPI_HandleTypeDef s_hspi;
static uint8_t           s_spi_ready;
static uint8_t           s_bus_hw;       /* 0 = soft (GPIO), 1 = HW SPI1    */

void CS_SET(void)
{
    LCD_CS_SET;
}
void CS_CLR(void)
{
    LCD_CS_CLR;
}

/* Configure + init SPI1 (once). GPIO re-mux happens in LCD_UseHwBus. */
static void spi_hw_init(void)
{
    if (s_spi_ready != 0U)
    {
        return;
    }
    __HAL_RCC_SPI1_CLK_ENABLE();

    s_hspi.Instance               = SPI1;
    s_hspi.Init.Mode              = SPI_MODE_MASTER;
    s_hspi.Init.Direction         = SPI_DIRECTION_1LINE;      /* TX only */
    s_hspi.Init.DataSize          = SPI_DATASIZE_8BIT;
    s_hspi.Init.CLKPolarity       = SPI_POLARITY_HIGH;        /* mode 3  */
    s_hspi.Init.CLKPhase          = SPI_PHASE_2EDGE;
    s_hspi.Init.NSS               = SPI_NSS_SOFT;
    s_hspi.Init.BaudRatePrescaler = LCD_SPI1_PRESC;
    s_hspi.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    s_hspi.Init.TIMode            = SPI_TIMODE_DISABLE;
    s_hspi.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    s_hspi.Init.CRCPolynomial     = 7U;
    (void)HAL_SPI_Init(&s_hspi);
    s_spi_ready = 1U;
}

/* Push all buffered bytes through SPI1 (blocking). */
void SPI_HW_Flush(void)
{
    if (s_tx_len != 0U)
    {
        if (HAL_SPI_Transmit(&s_hspi, s_tx_buf, (uint32_t)s_tx_len,
                             HAL_MAX_DELAY) != HAL_OK)
        {
            printf("[LCD] SPI TX FAIL len=%lu\r\n", (unsigned long)s_tx_len);
        }
        s_tx_len = 0U;
    }
}

/* ---------------- bus selection ------------------------------------- */

void LCD_UseSoftBus(void)
{
    GPIO_InitTypeDef g;

    SPI_HW_Flush();
    s_bus_hw = 0U;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Pull  = GPIO_NOPULL;
    g.Pin   = LCD_SCL_Pin | LCD_SDA_Pin;
    HAL_GPIO_Init(GPIOA, &g);
    /* idle SCL/SDA high */
    LCD_SPI_SCL_SET;
    LCD_SPI_SDA_SET;
}

void LCD_UseHwBus(void)
{
    GPIO_InitTypeDef g;

    s_bus_hw = 1U;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    g.Mode      = GPIO_MODE_AF_PP;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Pull      = GPIO_NOPULL;
    g.Alternate = GPIO_AF5_SPI1;         /* PA5 = SCK, PA7 = MOSI */
    g.Pin       = LCD_SCL_Pin | LCD_SDA_Pin;
    HAL_GPIO_Init(GPIOA, &g);

    spi_hw_init();
}

uint8_t LCD_BusIsHw(void)
{
    return s_bus_hw;
}

/* Active SPI1 baud in kHz (for the info page), e.g. 50000 = 50 MHz. */
unsigned long LCD_HwSpiKHz(void)
{
    uint32_t div = 2U << ((s_hspi.Init.BaudRatePrescaler & SPI_CR1_BR) >>
                          SPI_CR1_BR_Pos);
    return (unsigned long)(HAL_RCC_GetPCLK2Freq() / div / 1000U);
}

/* ---------------- SOFT path: bit-bang PA5/PA7 ------------------------ */

static void SendDataSPI(uint8_t dat)
{
    for (int i = 0; i < 8; i++)
    {
        if ((dat & 0x80U) != 0U)
        {
            LCD_SPI_SDA_SET;
        }
        else
        {
            LCD_SPI_SDA_CLR;
        }
        dat <<= 1;
        LCD_SPI_SCL_CLR;
        LCD_SPI_SCL_SET;
    }
}

/* ---------------- byte-level transfers ------------------------------- */

/* Send one byte immediately (commands): bus-dependent, no buffering. */
static void spi_send_now(uint8_t dat)
{
    if (s_bus_hw != 0U)
    {
        uint8_t b = dat;
        (void)HAL_SPI_Transmit(&s_hspi, &b, 1U, HAL_MAX_DELAY);
    }
    else
    {
        SendDataSPI(dat);
    }
}

/* Queue/send one data byte (DC already high). */
static void spi_put(uint8_t dat)
{
    if (s_bus_hw == 0U)
    {
        SendDataSPI(dat);
        return;
    }
    s_tx_buf[s_tx_len++] = dat;
    if (s_tx_len >= SPI_TX_BUF_SIZE)
    {
        SPI_HW_Flush();
    }
}

/* Write a register/command: DC low. */
void WriteComm(uint16_t data)
{
    SPI_HW_Flush();       /* drain pending data bytes (DC high) first */
    LCD_CS_CLR;
    LCD_RS_CLR;
    spi_send_now((uint8_t)data);
    LCD_CS_SET;
}

/* Write a data byte: DC high. */
void WriteData(uint16_t data)
{
    SPI_HW_Flush();
    LCD_CS_CLR;
    LCD_RS_SET;
    spi_send_now((uint8_t)data);
    LCD_CS_SET;
}

/* Stream one RGB565 pixel color as two 8-bit writes (DC high persists). */
void SendData(uint32_t color)
{
    LCD_CS_CLR;
    LCD_RS_SET;
    spi_put((uint8_t)(color >> 8));
    spi_put((uint8_t)color);
    SPI_HW_Flush();
    LCD_CS_SET;
}

/* Fast raw 8-bit data byte: no CS/DC toggling (caller manages both). */
void LCD_WriteDataFast(uint8_t data)
{
    spi_put(data);
}

/* Begin/end a data-gram burst for raster fills: DC high, CS held low. */
void LCD_BeginData(void)
{
    SPI_HW_Flush();       /* never mix a command byte into a data burst */
    LCD_CS_CLR;
    LCD_RS_SET;
}
void LCD_EndData(void)
{
    SPI_HW_Flush();
    LCD_CS_SET;
}