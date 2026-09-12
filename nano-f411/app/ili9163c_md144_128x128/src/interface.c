/*
  interface.c - low-level ILI9163C bus primitives (nano-f411 port).

  3-wire serial (the module connector has no D/C pin): every byte is a
  9-bit frame - the D/C bit (0 = command, 1 = data) is clocked first, then
  the 8 data bits, MSB first. Both bit types use the same edge convention:
  the line is set while SCL is high, then SCL falls and rises (panel
  latches on the rising edge).

  Two drive methods:

    SOFT: PA5/PA7 bit-banged, one 9-bit frame per byte.

    HW  : SPI1 AF5 (PA5 = SCK, PA7 = MOSI), mode 3 (CPOL=1, CPHA=1 - same
          idle-high / rising-edge-sampling timing the bit-bang produces),
          **16-bit frames carrying a packed 9-bit-frame bitstream**. The
          F411 SPI cannot produce native 9-bit frames (DFF is strictly
          8/16-bit), so frames are packed into 16-bit words: the panel
          only observes SCL/SDA and counts its own 9-bit boundaries, so
          the packed stream is transparent to it. At the end of a burst
          the pending bits (< 9, so no extra frame can complete) are
          padded into a final word and discarded by the panel when CS
          rises. Default prescaler /8 = 12.5 MHz SCK (APB2 = 100 MHz via
          the project clock override); LCD_SPI1_PRESC selects /4 = 25 MHz.

    SCL = PA5, SDA = PA7, RES = PA6, CS = PB8 (GPIO). CS frames each
    command; data bursts hold CS low across bytes.
*/

#include "interface.h"
#include <stdio.h>
#include "lcd.h"
#include "stm32f4xx_hal.h"

/* ---------------- shared state ---------------- */
static uint8_t s_bus_hw;         /* 0 = soft (GPIO), 1 = HW SPI1        */

/* ---------------- SOFT path ---------------- */

/* Clock out one bit (set while SCL high, latch on the rising edge). */
static void SendBit(uint8_t v)
{
    if (v != 0U)
    {
        LCD_SPI_SDA_SET;
    }
    else
    {
        LCD_SPI_SDA_CLR;
    }
    LCD_SPI_SCL_CLR;
    LCD_SPI_SCL_SET;
}

/* Shift out one 8-bit byte, MSB first. */
static void SendByte(uint8_t dat)
{
    for (int i = 0; i < 8; i++)
    {
        SendBit((dat & 0x80U) != 0U);
        dat <<= 1;
    }
}

/* ---------------- HW path: SPI1 with packed 9-bit frames ---------------- */

/* 16-bit words of packed bitstream per HAL_SPI_Transmit call. */
#define SPI_TX_WORDS 256U

/* Baud prescaler: APB2 = 100 MHz (project clock override). The default
 * /8 = 12.5 MHz SCK is within the panel's write-cycle spec; /4 = 25 MHz
 * via LCD_SPI1_PRESC if a module proves it can take it. */
#ifndef LCD_SPI1_PRESC
#define LCD_SPI1_PRESC SPI_BAUDRATEPRESCALER_8
#endif

static SPI_HandleTypeDef s_hspi;
static uint8_t           s_spi_ready;
static uint16_t          s_tx_words[SPI_TX_WORDS];
static uint16_t          s_tx_len;

/* 9-bit frame accumulator: frames are appended MSB-first into a bitstream
 * that drains into 16-bit SPI words. */
static uint32_t s_acc;
static uint32_t s_nbits;

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
    s_hspi.Init.DataSize          = SPI_DATASIZE_16BIT;       /* packed  */
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

/* Append one 9-bit frame (dc, byte) to the packed bitstream. */
static void hw_put_frame(uint8_t dc, uint8_t byte)
{
    s_acc = (s_acc << 9U) | ((uint32_t)(dc != 0U) << 8U) | (uint32_t)byte;
    s_nbits += 9U;

    while (s_nbits >= 16U)
    {
        s_nbits -= 16U;
        s_tx_words[s_tx_len++] = (uint16_t)((s_acc >> s_nbits) & 0xFFFFU);
        if (s_tx_len >= SPI_TX_WORDS)
        {
            SPI_HW_Flush();
        }
    }
    s_acc &= (s_nbits != 0U) ? ((1U << s_nbits) - 1U) : 0U;
}

/* Pad the pending bits (< 9, cannot complete an extra frame - the panel
 * discards them when CS rises) into a final word, then transmit. */
static void hw_pad_flush(void)
{
    if (s_nbits > 0U)
    {
        s_tx_words[s_tx_len++] =
            (uint16_t)((s_acc << (16U - s_nbits)) & 0xFFFFU);
        s_nbits = 0U;
        s_acc   = 0U;
    }
    SPI_HW_Flush();
}

/* Push all buffered 16-bit words through SPI1 (blocking). */
void SPI_HW_Flush(void)
{
    if (s_tx_len != 0U)
    {
        if (HAL_SPI_Transmit(&s_hspi, (uint8_t *)s_tx_words, s_tx_len,
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
    s_acc    = 0U;
    s_nbits  = 0U;
    s_tx_len = 0U;

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

/* Active SPI1 baud in kHz (for the info page), e.g. 12500 = 12.5 MHz. */
unsigned long LCD_HwSpiKHz(void)
{
    uint32_t div = 2U << ((s_hspi.Init.BaudRatePrescaler & SPI_CR1_BR) >>
                          SPI_CR1_BR_Pos);
    return (unsigned long)(HAL_RCC_GetPCLK2Freq() / div / 1000U);
}

/* ---------------- byte-level transfers ---------------- */

/* Write a register/command: 9-bit frame with D/C = 0. */
void WriteComm(uint16_t data)
{
    SPI_HW_Flush();
    s_acc   = 0U;                    /* frame starts bit-aligned */
    s_nbits = 0U;
    LCD_CS_CLR;

    if (s_bus_hw != 0U)
    {
        hw_put_frame(0U, (uint8_t)data);
        hw_pad_flush();
    }
    else
    {
        SendBit(0U);
        SendByte((uint8_t)data);
    }
    LCD_CS_SET;
}

/* Write a data byte: 9-bit frame with D/C = 1. */
void WriteData(uint16_t data)
{
    SPI_HW_Flush();
    s_acc   = 0U;
    s_nbits = 0U;
    LCD_CS_CLR;

    if (s_bus_hw != 0U)
    {
        hw_put_frame(1U, (uint8_t)data);
        hw_pad_flush();
    }
    else
    {
        SendBit(1U);
        SendByte((uint8_t)data);
    }
    LCD_CS_SET;
}

/* One 16-bit color as two D/C=1 frames, no CS toggling (the callers -
 * the TEST_STAND fill bodies - hold CS low around whole fills). */
void SendData(uint32_t color)
{
    if (s_bus_hw != 0U)
    {
        hw_put_frame(1U, (uint8_t)(color >> 8));
        hw_put_frame(1U, (uint8_t)color);
        SPI_HW_Flush();
    }
    else
    {
        SendBit(1U);
        SendByte((uint8_t)(color >> 8));
        SendBit(1U);
        SendByte((uint8_t)color);
    }
}

/* Fast raw 8-bit data byte as a D/C=1 frame: caller manages CS
 * (LCD_BeginData/LCD_EndData frame the burst). */
void LCD_WriteDataFast(uint8_t data)
{
    if (s_bus_hw != 0U)
    {
        hw_put_frame(1U, data);
    }
    else
    {
        SendBit(1U);
        SendByte(data);
    }
}

/* Begin/end a raster burst: CS held low across the bytes. */
void LCD_BeginData(void)
{
    SPI_HW_Flush();
    s_acc   = 0U;                    /* start the burst bit-aligned */
    s_nbits = 0U;
    LCD_CS_CLR;
}
void LCD_EndData(void)
{
    if (s_bus_hw != 0U)
    {
        hw_pad_flush();
    }
    s_acc   = 0U;
    s_nbits = 0U;
    LCD_CS_SET;
}