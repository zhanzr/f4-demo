/*
  interface.c - low-level JD9851 bus primitives (nano-f411 port).

  Hardware SPI1 only (the vendor example likewise drives the panel from
  the SPI peripheral): PA5/PA7 re-muxed to SPI1 AF5 (SCK/MOSI), master,
  full duplex, 8-bit, mode 3 (CPOL=1, CPHA=2EDGE - the vendor's F103
  hard-SPI timing), MSB first, NSS soft. APB2 = 100 MHz (project clock
  override, main.c): /2 = 50 MHz SCK, the fastest SPI1 can run here.

  JD9851 framing (vendor-verbatim): plain DC-based 4-wire SPI - the
  command byte goes out with DC(RS)=PA6 low, every data byte with DC
  high. CS frames a command+its data bytes (the vendor actually leaves
  CS low across whole command sequences; framing each transaction is a
  superset that the panel accepts).

  HW note (learned on the nv3030b panel, same rule here): SPE must be
  enabled BEFORE CS goes low. With SPE=0 the SCK pin idles low; setting
  SPE with CPOL=1 drives it high - a rising SCL edge the panel would
  latch as a spurious first bit while selected.
*/

#include "interface.h"
#include <stdio.h>
#include "lcd.h"
#include "stm32f4xx_hal.h"

/* Bytes of pixel data accumulated before one SPI1 burst. */
#define SPI_TX_BUF_SIZE 512U

/* SPI1 baud prescaler: APB2 = 100 MHz (project clock override, see
 * main.c SystemClock_Config). /2 = 50 MHz SCK; back off via
 * LCD_SPI1_PRESC if the wire misbehaves. */
#ifndef LCD_SPI1_PRESC
#define LCD_SPI1_PRESC SPI_BAUDRATEPRESCALER_2
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

/* Configure + init SPI1 (once). */
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

/* Ensure the SPI is enabled (SPE) - see the framing note above. */
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

/* Active SPI1 baud in kHz (for the info page). */
unsigned long LCD_HwSpiKHz(void)
{
    uint32_t div = 2U << ((s_hspi.Init.BaudRatePrescaler & SPI_CR1_BR) >>
                          SPI_CR1_BR_Pos);
    return (unsigned long)(HAL_RCC_GetPCLK2Freq() / div / 1000U);
}

/* ---------------- byte-level transfers ------------------------------- */

/* Queue one data byte (framing already open, DC already high). */
static void spi_put(uint8_t dat)
{
    s_tx_buf[s_tx_len++] = dat;
    if (s_tx_len >= SPI_TX_BUF_SIZE)
    {
        SPI_HW_Flush();
    }
}

/* JD9851 command: CS high (settle), SPE on while CS is high, CS low,
 * DC low, the command byte, DC high again - the data bytes that follow
 * (same CS frame) then go out with DC high (vendor framing). */
void WriteComm(uint16_t data)
{
    uint8_t cmd = (uint8_t)data;
    SPI_HW_Flush();
    LCD_CS_SET;
    for (volatile int d = 0; d < 20; d++) { }   /* CS high settle */
    spi_hw_enable();
    LCD_CS_CLR;
    LCD_RS_CLR;
    spi_hw_tx(&cmd, 1U);
    LCD_RS_SET;
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

/* Solid-color bulk burst into the open frame: a tight register-level
 * loop that writes DR directly (no per-byte calls, no buffer). At
 * 50 MHz the wire needs 160 ns/byte; the loop's poll+store fits in
 * that window, so the SPI - not the CPU - is the bottleneck. */
void LCD_FillBulk(uint32_t color, uint32_t pixels)
{
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)color;

    SPI_HW_Flush();
    LCD_RS_SET;
    spi_hw_enable();
    while (pixels-- != 0U)
    {
        while ((SPI1->SR & SPI_SR_TXE) == 0U) { }
        *((__IO uint8_t *)&SPI1->DR) = hi;
        while ((SPI1->SR & SPI_SR_TXE) == 0U) { }
        *((__IO uint8_t *)&SPI1->DR) = lo;
    }
    while ((SPI1->SR & SPI_SR_BSY) != 0U) { }
    if ((SPI1->SR & SPI_SR_OVR) != 0U)
    {
        (void)SPI1->DR;
        (void)SPI1->SR;
    }
}

/* Begin/end a raster burst. The caller issues WriteComm(0x2C) first;
 * BeginData just makes sure CS is low and DC is high so the burst
 * stays in one data frame; EndData closes it. */
void LCD_BeginData(void)
{
    SPI_HW_Flush();
    LCD_CS_CLR;
    LCD_RS_SET;
}
void LCD_EndData(void)
{
    SPI_HW_Flush();
    LCD_CS_SET;
}
