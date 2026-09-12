/*
  interface.c - low-level ST7365P bus primitives (nano-f411 port).
  One wiring (SCL=PA5, SDA=PA7, MISO=PA6), two drive methods selected at
  runtime:

    SOFT: PA5/PA7 re-muxed to plain GPIO outputs and bit-banged (DC
          selects command vs data, CS asserts per transfer). Reads drive
          SDA high (release) and sample it after each falling edge.
    HW  : PA5/PA6/PA7 re-muxed to SPI1 AF5 (SCK/MISO/MOSI), master,
          FULL_DUPLEX (so the MISO line can be read), 8-bit, mode 3
          (CPOL=1, CPHA=1 - the same idle-high / rising-edge-sampling
          timing the bit-bang produces), MSB first, NSS soft. DC=PA4 /
          RES=PA3 / CS=PB8 stay GPIO. All HW transfers use
          HAL_SPI_TransmitReceive (TX = payload, RX = sink), which keeps
          the RX side drained on every operation.
          APB2 = 100 MHz (project clock override): default prescaler
          /2 = 50 MHz SCK (the F411 SPI1 max); /4 = 25 MHz available via
          LCD_SPI1_PRESC.

  Raster bursts in HW mode stream through a 512-byte TX buffer (one
  HAL_SPI_TransmitReceive per <=512 bytes); in SOFT mode bytes go
  straight to the bit-bang.  CS is asserted per transfer, DC selects
  command vs data.

    SCL = PA5, SDA = PA7, DC = PA4, RES = PA3, CS = PB8 (GPIO).
    MISO = PA6: read-back line (panel ID etc.).
*/

#include "interface.h"
#include <stdio.h>
#include "lcd.h"
#include "stm32f4xx_hal.h"

/* Bytes of pixel data accumulated before one HAL_SPI_TransmitReceive
 * call (HW). */
#define SPI_TX_BUF_SIZE 512U

/* SPI1 baud prescaler: APB2 = 100 MHz (project clock override). The
 * default /2 = 50 MHz SCK is the F411 SPI1 max; /4 = 25 MHz via
 * LCD_SPI1_PRESC if a module needs a slower rate. */
#ifndef LCD_SPI1_PRESC
#define LCD_SPI1_PRESC SPI_BAUDRATEPRESCALER_2
#endif

static uint8_t           s_tx_buf[SPI_TX_BUF_SIZE];
static uint8_t           s_rx_sink[SPI_TX_BUF_SIZE];
static uint16_t          s_tx_len;
static uint8_t           s_burst16;   /* 1 = inside a raster burst (16-bit
                                       * frames carry 2 bytes each)       */
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

/* Push all buffered bytes through SPI1 (blocking). Fast register-level
 * polled TX: per-byte cost is a TXE wait + DR write; the MISO side is not
 * collected during bursts (it carries no TX-relevant data). After the
 * burst the RX side is drained and a possible overrun is cleared, so the
 * next full-duplex operation (ID read) starts with a clean RX state. */
/* Ensure the SPI is enabled: HAL transfers disable SPE when they close,
 * and the register-level TX below relies on SPE being set (with SPE=0 the
 * TX buffer never drains and TXE stays low). */
static void spi_hw_enable(void)
{
    if ((SPI1->CR1 & SPI_CR1_SPE) == 0U)
    {
        SPI1->CR1 |= SPI_CR1_SPE;
    }
}

/* Transmit len bytes: inside a raster burst (s_burst16) pairs of bytes
 * go out as 16-bit frames (the panel counts 8 clocks per byte and sees a
 * plain byte stream), which halves the per-byte DR-write overhead. Any
 * odd trailing byte goes out in 8-bit mode. DFF may only be changed with
 * SPE = 0, so every mode switch disables/re-enables the SPI around it. */
static void spi_hw_tx(uint8_t *buf, uint16_t len)
{
    uint16_t i = 0;

    if (s_burst16)
    {
        uint16_t even = len & ~1U;

        if (even != 0U)
        {
            __HAL_SPI_DISABLE(&s_hspi);
            SPI1->CR1 |= SPI_CR1_DFF;        /* 16-bit frames */
            __HAL_SPI_ENABLE(&s_hspi);

            while (i < even)
            {
                while ((SPI1->SR & SPI_SR_TXE) == 0U) { }
                *(volatile uint16_t *)&SPI1->DR =
                    (uint16_t)(((uint16_t)buf[i] << 8) | buf[i + 1]);
                i += 2;
            }
            while ((SPI1->SR & SPI_SR_BSY) != 0U) { }
            while ((SPI1->SR & SPI_SR_RXNE) != 0U) { (void)SPI1->DR; }
            if ((SPI1->SR & SPI_SR_OVR) != 0U)
            {
                (void)SPI1->DR;
                (void)SPI1->SR;
            }

            __HAL_SPI_DISABLE(&s_hspi);
            SPI1->CR1 &= ~SPI_CR1_DFF;       /* back to 8-bit frames */
            __HAL_SPI_ENABLE(&s_hspi);
        }

        if (i < len)                          /* odd trailing byte */
        {
            while ((SPI1->SR & SPI_SR_TXE) == 0U) { }
            *((__IO uint8_t *)&SPI1->DR) = buf[i];
            while ((SPI1->SR & SPI_SR_BSY) != 0U) { }
            while ((SPI1->SR & SPI_SR_RXNE) != 0U) { (void)SPI1->DR; }
            if ((SPI1->SR & SPI_SR_OVR) != 0U)
            {
                (void)SPI1->DR;
                (void)SPI1->SR;
            }
        }
        return;
    }

    spi_hw_enable();
    for (; i < len; i++)
    {
        while ((SPI1->SR & SPI_SR_TXE) == 0U) { }
        *((__IO uint8_t *)&SPI1->DR) = buf[i];
    }
    while ((SPI1->SR & SPI_SR_BSY) != 0U) { }

    while ((SPI1->SR & SPI_SR_RXNE) != 0U) { (void)SPI1->DR; }
    if ((SPI1->SR & SPI_SR_OVR) != 0U)
    {
        (void)SPI1->DR;
        (void)SPI1->SR;
    }
}

void SPI_HW_Flush(void)
{
    if (s_tx_len != 0U)
    {
        spi_hw_tx(s_tx_buf, s_tx_len);
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
    g.Alternate = GPIO_AF5_SPI1;         /* PA5 = SCK, PA7 = MOSI, PA6 = MISO */
    g.Pin       = LCD_SCL_Pin | LCD_SDA_Pin | GPIO_PIN_6;
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

/* Configure the MISO line (PA6) as input for soft read-back. */
static void miso_dir_input(void)
{
    GPIO_InitTypeDef g;

    g.Pin   = LCD_MISO_Pin;
    g.Mode  = GPIO_MODE_INPUT;
    g.Pull  = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LCD_GPIO_PortMISO, &g);
}

/* Read one 8-bit byte from MISO: SCL falls (panel shifts the next bit
 * out), then the bit is sampled before the rising edge. */
static uint8_t SoftReadByte(void)
{
    uint8_t dat = 0;

    for (int i = 0; i < 8; i++)
    {
        uint8_t bit;

        LCD_SPI_SCL_CLR;
        for (volatile int d = 0; d < 20; d++) { }   /* let MISO settle  */
        bit = (LCD_GPIO_PortMISO->IDR & LCD_MISO_Pin) ? 1U : 0U;
        LCD_SPI_SCL_SET;
        dat = (uint8_t)((dat << 1) | bit);
    }
    return dat;
}

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
        uint8_t rx;
        spi_hw_enable();
        while ((SPI1->SR & SPI_SR_TXE) == 0U) { }
        *((__IO uint8_t *)&SPI1->DR) = dat;
        while ((SPI1->SR & SPI_SR_BSY) != 0U) { }
        while ((SPI1->SR & SPI_SR_RXNE) != 0U) { rx = (uint8_t)SPI1->DR; }
        if ((SPI1->SR & SPI_SR_OVR) != 0U)
        {
            (void)SPI1->DR;
            (void)SPI1->SR;
        }
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
    SPI_HW_Flush();
    s_burst16 = 0U;       /* commands always go out as 8-bit frames */
    LCD_CS_CLR;
    LCD_RS_CLR;
    spi_send_now((uint8_t)data);
    LCD_CS_SET;
}

/* Write a data byte: DC high. */
void WriteData(uint16_t data)
{
    SPI_HW_Flush();
    s_burst16 = 0U;
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

/* Begin/end a data-gram burst for raster fills: DC high, CS held low.
 * Inside the burst HW transfers use 16-bit frames (2 bytes each). */
void LCD_BeginData(void)
{
    SPI_HW_Flush();       /* never mix a command byte into a data burst */
    s_burst16 = (s_bus_hw != 0U) ? 1U : 0U;
    LCD_CS_CLR;
    LCD_RS_SET;
}
void LCD_EndData(void)
{
    s_burst16 = 0U;
    SPI_HW_Flush();
    LCD_CS_SET;
}

/* ---------------- read-back (MISO) ----------------------------------- */

/* Read panel registers over the MISO line (both buses):
 *   cmd   - read command (e.g. 0x04 RDDID)
 *   out   - receives the nread bytes read after nskip dummy bytes
 * The SDA line is released to the panel for the read phase. On HW the
 * read is a true full-duplex HAL transfer (RX matters here).            */
void LCD_ReadBytes(uint8_t cmd, uint8_t *out, uint8_t nskip, uint8_t nread)
{
    SPI_HW_Flush();
    LCD_CS_CLR;
    LCD_RS_CLR;
    spi_send_now(cmd);
    LCD_RS_SET;

    if (s_bus_hw != 0U)
    {
        uint8_t tx[8]   = { 0xFFU, 0xFFU, 0xFFU, 0xFFU,
                            0xFFU, 0xFFU, 0xFFU, 0xFFU };
        uint8_t rx[8]   = { 0 };
        uint16_t n = (uint16_t)(nskip + nread);

        if (n > 8U) { n = 8U; }
        (void)HAL_SPI_TransmitReceive(&s_hspi, tx, rx, n, HAL_MAX_DELAY);
        for (uint8_t i = 0; i < nread; i++)
        {
            out[i] = rx[nskip + i];
        }
    }
    else
    {
        /* release SDA (host output) so it cannot fight the panel's
         * read-back, and sample the MISO line */
        GPIO_InitTypeDef g;

        g.Pin   = LCD_SDA_Pin;
        g.Mode  = GPIO_MODE_INPUT;
        g.Pull  = GPIO_PULLUP;
        g.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(LCD_GPIO_PortSDA, &g);

        miso_dir_input();
        for (uint8_t i = 0; i < nskip; i++)
        {
            (void)SoftReadByte();
        }
        for (uint8_t i = 0; i < nread; i++)
        {
            out[i] = SoftReadByte();
        }

        g.Pin   = LCD_SDA_Pin;
        g.Mode  = GPIO_MODE_OUTPUT_PP;
        g.Pull  = GPIO_NOPULL;
        HAL_GPIO_Init(LCD_GPIO_PortSDA, &g);
        LCD_SPI_SDA_SET;
    }
    LCD_CS_SET;
}