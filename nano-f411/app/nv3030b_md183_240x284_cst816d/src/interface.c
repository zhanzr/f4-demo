/*
  interface.c - low-level NV3030B bus primitives (nano-f411 port).

  Hardware SPI1 only (the vendor example likewise drives the panel from
  the SPI peripheral - there is no software bit-bang in it):

    HW: PA5/PA7 re-muxed to SPI1 AF5 (SCK/MOSI), master, simplex TX
        (1LINE), 8-bit, mode 3 (CPOL=1, CPHA=1 - the same idle-high /
        rising-edge-sampling timing the vendor's F103 hard-SPI uses),
        MSB first, NSS soft. CS=PA4 stays GPIO. APB2 = 50 MHz (board
        default clock tree): default prescaler /4 = 12.5 MHz SCK to
        isolate bring-up; /2 = 25 MHz available via LCD_SPI1_PRESC once
        verified.

  NV3030B wrapped-command framing (vendor-verbatim): every command is
  written as CS high (settle), CS low, then four bytes 02 00 <cmd> 00;
  parameter and pixel bytes then stream into the same CS frame. The
  module's DC pin is not part of this protocol (the vendor never drives
  it), and there is no reset pin - power-cycling the module is the only
  recovery from a latched state.
*/

#include "interface.h"
#include <stdio.h>
#include "lcd.h"
#include "stm32f4xx_hal.h"

/* Bytes of pixel data accumulated before one HAL_SPI_Transmit call (HW). */
#define SPI_TX_BUF_SIZE 512U

/* SPI1 baud prescaler: APB2 = 50 MHz (board default clock tree). The
 * default /4 = 12.5 MHz SCK isolates bring-up; /2 = 25 MHz via
 * LCD_SPI1_PRESC once verified. */
#ifndef LCD_SPI1_PRESC
#define LCD_SPI1_PRESC SPI_BAUDRATEPRESCALER_4
#endif

static uint8_t           s_tx_buf[SPI_TX_BUF_SIZE];
static uint16_t          s_tx_len;
static SPI_HandleTypeDef s_hspi;
static uint8_t           s_spi_ready;

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
    s_hspi.Init.Direction         = SPI_DIRECTION_2LINES;     /* full duplex */
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

/* Ensure the SPI is enabled: HAL transfers disable SPE when they close,
 * and the register-level TX below relies on SPE being set. */
static void spi_hw_enable(void)
{
    if ((SPI1->CR1 & SPI_CR1_SPE) == 0U)
    {
        SPI1->CR1 |= SPI_CR1_SPE;
    }
}

/* Fast register-level polled TX: per-byte cost is a TXE wait + DR write;
 * the MISO side is not collected (this module is write-only). A possible
 * overrun is cleared after the burst. */
static void spi_hw_tx(uint8_t *buf, uint16_t len)
{
    spi_hw_enable();
    for (uint16_t i = 0; i < len; i++)
    {
        while ((SPI1->SR & SPI_SR_TXE) == 0U) { }
        *((__IO uint8_t *)&SPI1->DR) = buf[i];
    }
    while ((SPI1->SR & SPI_SR_BSY) != 0U) { }

    if ((SPI1->SR & SPI_SR_OVR) != 0U)
    {
        (void)SPI1->DR;
        (void)SPI1->SR;
    }
}

/* Push all buffered bytes through SPI1 (blocking). */
void SPI_HW_Flush(void)
{
    if (s_tx_len != 0U)
    {
        spi_hw_tx(s_tx_buf, s_tx_len);
        s_tx_len = 0U;
    }
}

/* ---------------- bus selection ------------------------------------- */

/* Mux PA5/PA7 to SPI1 AF5 and init the peripheral (idempotent). */
void LCD_UseHwBus(void)
{
    GPIO_InitTypeDef g;

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
    return 1U;
}

/* Active SPI1 baud in kHz (for the info page), e.g. 12500 = 12.5 MHz. */
unsigned long LCD_HwSpiKHz(void)
{
    uint32_t div = 2U << ((s_hspi.Init.BaudRatePrescaler & SPI_CR1_BR) >>
                          SPI_CR1_BR_Pos);
    return (unsigned long)(HAL_RCC_GetPCLK2Freq() / div / 1000U);
}

/* ---------------- byte-level transfers ------------------------------- */

/* Send one byte immediately: register-level polled TX. */
static void spi_send_now(uint8_t dat)
{
    spi_hw_tx(&dat, 1U);
}

/* Queue/send one data byte (framing already open). */
static void spi_put(uint8_t dat)
{
    s_tx_buf[s_tx_len++] = dat;
    if (s_tx_len >= SPI_TX_BUF_SIZE)
    {
        SPI_HW_Flush();
    }
}

/* NV3030B wrapped command: CS high (settle), CS low, then the 4-byte
 * prefix 02 00 <cmd> 00. The frame stays open for the data bytes that
 * follow (vendor-verbatim framing). */
void WriteComm(uint16_t data)
{
    SPI_HW_Flush();
    LCD_CS_SET;
    for (volatile int d = 0; d < 20; d++) { }   /* CS high settle */
    LCD_CS_CLR;
    spi_send_now(0x02U);
    spi_send_now(0x00U);
    spi_send_now((uint8_t)data);
    spi_send_now(0x00U);
}

/* Write one data byte into the open frame. */
void WriteData(uint16_t data)
{
    spi_put((uint8_t)data);
}

/* Stream one RGB565 pixel color as two bytes into the open frame. */
void SendData(uint32_t color)
{
    spi_put((uint8_t)(color >> 8));
    spi_put((uint8_t)color);
}

/* Fast raw 8-bit data byte: streams into the open frame. */
void LCD_WriteDataFast(uint8_t data)
{
    spi_put(data);
}

/* Begin/end a raster burst. The caller issues WriteComm(0x2C) first;
 * BeginData just makes sure CS is low and the burst stays in one frame;
 * EndData closes it. */
void LCD_BeginData(void)
{
    SPI_HW_Flush();
    LCD_CS_CLR;
}
void LCD_EndData(void)
{
    SPI_HW_Flush();
    LCD_CS_SET;
}