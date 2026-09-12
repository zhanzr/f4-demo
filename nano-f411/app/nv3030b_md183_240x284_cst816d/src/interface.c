/*
  interface.c - low-level NV3030B bus primitives (nano-f411 port).

  Two transports behind one byte-level API:

    SOFT: PA5/PA7 as plain GPIO outputs, bit-banged exactly like the
          vendor's TK499 soft-SPI example (clock low, set SDA, clock
          high; MSB first), unpaced like the st7365 project (~2 MHz).

    HW:   PA5/PA7 re-muxed to SPI1 AF5 (SCK/MOSI), master, full duplex,
          8-bit, mode 3 (CPOL=1, CPHA=2EDGE - the same idle-high /
          rising-edge-sampling timing the vendor's F103 hard-SPI uses),
          MSB first, NSS soft. APB2 = 100 MHz (project clock override,
          main.c): /2 = 50 MHz SCK, the fastest SPI1 can run here.

  NV3030B wrapped-command framing (vendor-verbatim): every command is
  written as CS high (settle), CS low, then four bytes 02 00 <cmd> 00;
  parameter and pixel bytes then stream into the same CS frame. The
  module's DC pin (PA6) is not part of this protocol - it is driven
  push-pull LOW like the vendor leaves it, but never toggled. There is
  no reset pin - power-cycling the module is the only recovery from a
  latched state.
*/

#include "interface.h"
#include <stdio.h>
#include "lcd.h"
#include "stm32f4xx_hal.h"

/* Bytes of pixel data accumulated before one SPI1 burst (HW path only). */
#define SPI_TX_BUF_SIZE 512U

/* SPI1 baud prescaler: APB2 = 100 MHz (project clock override, see
 * main.c SystemClock_Config). /2 = 50 MHz SCK - the fastest SPI1 can
 * run here (the prescaler has no /1 setting). */
#ifndef LCD_SPI1_PRESC
#define LCD_SPI1_PRESC SPI_BAUDRATEPRESCALER_2
#endif

static uint8_t           s_tx_buf[SPI_TX_BUF_SIZE];
static uint16_t          s_tx_len;
static SPI_HandleTypeDef s_hspi;
static uint8_t           s_spi_ready;
static uint8_t           s_bus_hw = 1U;   /* 1 = SPI1, 0 = bit-bang */

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

/* Push all buffered bytes through SPI1 (blocking; no-op on the soft bus). */
void SPI_HW_Flush(void)
{
    if (s_bus_hw != 0U && s_tx_len != 0U)
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

    SPI_HW_Flush();
    s_bus_hw = 1U;
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

/* PA5/PA7 back to plain push-pull outputs for the bit-bang path
 * (TK499-vendor style: clock low, set SDA, clock high). */
void LCD_UseSoftBus(void)
{
    GPIO_InitTypeDef g;

    s_bus_hw = 0U;
    s_tx_len = 0U;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Pull  = GPIO_NOPULL;
    g.Pin   = LCD_SCL_Pin | LCD_SDA_Pin;
    HAL_GPIO_Init(GPIOA, &g);

    LCD_SPI_SCL_SET;                     /* SCL idles high (mode 3-like) */
    LCD_SPI_SDA_CLR;
}

uint8_t LCD_BusIsHw(void)
{
    return s_bus_hw;
}

/* Active SPI1 baud in kHz (for the info page). */
unsigned long LCD_HwSpiKHz(void)
{
    uint32_t div = 2U << ((s_hspi.Init.BaudRatePrescaler & SPI_CR1_BR) >>
                          SPI_CR1_BR_Pos);
    return (unsigned long)(HAL_RCC_GetPCLK2Freq() / div / 1000U);
}

/* Soft-bus rate in kHz (for the info page); measured from solid-fill
 * wire time on this board (unpaced bit-bang). */
unsigned long LCD_SoftKHz(void)
{
    return 2100UL;
}

/* ---------------- byte-level transfers ------------------------------- */

static void soft_tx(uint8_t *buf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
    {
        uint8_t b = buf[i];
        for (int k = 0; k < 8; k++)
        {
            LCD_SPI_SCL_CLR;
            if ((b & 0x80U) != 0U) { LCD_SPI_SDA_SET; } else { LCD_SPI_SDA_CLR; }
            LCD_SPI_SCL_SET;
            b = (uint8_t)(b << 1);
        }
    }
}

/* Send byte(s) immediately on the active bus. */
static void spi_send_now(uint8_t *buf, uint16_t len)
{
    if (s_bus_hw != 0U) { spi_hw_tx(buf, len); }
    else                { soft_tx(buf, len); }
}

/* Queue/send one data byte (framing already open). */
static void spi_put(uint8_t dat)
{
    if (s_bus_hw == 0U)
    {
        soft_tx(&dat, 1U);
        return;
    }
    s_tx_buf[s_tx_len++] = dat;
    if (s_tx_len >= SPI_TX_BUF_SIZE)
    {
        SPI_HW_Flush();
    }
}

/* NV3030B wrapped command: CS high (settle), CS low, then the 4-byte
 * prefix 02 00 <cmd> 00. The frame stays open for the data bytes that
 * follow (vendor-verbatim framing).
 *
 * HW path: SPE must be enabled BEFORE CS goes low. With SPE=0 the SCK
 * pin idles low; setting SPE with CPOL=1 drives SCK high - a rising SCL
 * edge that the panel would latch as a spurious first bit while
 * selected, shifting the whole wrapped stream. Enable while CS is
 * still high instead; SCK just returns to its idle-high level. */
void WriteComm(uint16_t data)
{
    uint8_t pre[4];
    SPI_HW_Flush();
    LCD_CS_SET;
    for (volatile int d = 0; d < 20; d++) { }   /* CS high settle */
    if (s_bus_hw != 0U)
    {
        spi_hw_enable();
    }
    LCD_CS_CLR;
    pre[0] = 0x02U;
    pre[1] = 0x00U;
    pre[2] = (uint8_t)data;
    pre[3] = 0x00U;
    spi_send_now(pre, 4U);
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

    if (s_bus_hw == 0U)
    {
        while (pixels-- != 0U)
        {
            soft_tx(&hi, 1U);
            soft_tx(&lo, 1U);
        }
        return;
    }

    SPI_HW_Flush();
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