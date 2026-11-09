/*
  interface.c - low-level ILI9488 bus primitives (nucleo-f413 port).

  One wiring, two drive methods selected at runtime:
    SOFT: PA5/PA7 as plain GPIO outputs, bit-banged (DC pin selects
          command vs data), unpaced (~2 MHz on the 100 MHz board).
    HW:   PA5/PA7 re-muxed to SPI1 AF5 (SCK/MOSI), master, 8-bit, mode 0
          (CPOL=0, CPHA=1EDGE - matching the working Arduino DIYables
          ILI9488 driver), MSB first, NSS soft. The F413 board runs APB2 at 100 MHz natively:
          default prescaler /8 = 12.5 MHz SCK; /4 = 25 MHz and /2 =
          50 MHz via LCD_SPI1_PRESC if the module proves it.

  Raster bursts in HW mode stream through a 512-byte TX buffer (pairs of
  bytes go out as 16-bit frames - the panel counts 8 clocks per byte and
  sees a plain byte stream; the odd trailing byte goes out 8-bit). In
  SOFT mode bytes go straight to the bit-bang. CS is asserted per
  transfer, DC selects command vs data.

    SCL = PA5, MOSI = PA7, MISO = PA6, DC = PD15, RES = PF12,
    CS = PD14 (GPIO).
*/

#include <stdint.h>
#include "lcd.h"
#include <string.h>
#include "stm32f4xx_hal.h"

/* Bytes of pixel data accumulated before one SPI1 burst (HW path only).
 * In a raster burst pairs of bytes go out as 16-bit frames. */
#define SPI_TX_BUF_SIZE 4096U   /* DMA moves this in ~65 us at 50 MHz */

/* SPI1 baud prescaler: APB2 = 100 MHz natively on the F413 board clock
 * tree; /2 = 50 MHz SCK is the SPI1 maximum and the verified default.
 * Measured on this module: 25 and 50 MHz render clean, but 12.5 MHz
 * (DATAST-era /8) garbles - a specific SCK period can land in a bad
 * spot of the panel's internal clock-divider sampling. Avoid it. */
#ifndef LCD_SPI1_PRESC
#define LCD_SPI1_PRESC SPI_BAUDRATEPRESCALER_2
#endif

/* EXPERIMENTAL DMA raster bursts (see spi_hw_tx): default OFF - the
 * bursts complete but the panel shows nothing yet. */
#ifndef LCD_USE_DMA
#define LCD_USE_DMA 0
#endif

static uint8_t           s_tx_buf[SPI_TX_BUF_SIZE];
static uint8_t           s_rx_sink[SPI_TX_BUF_SIZE];
static uint16_t          s_tx_len;
static uint8_t           s_burst16;   /* 1 = inside a raster burst (16-bit
                                       * frames carry 2 bytes each)       */
static SPI_HandleTypeDef s_hspi;
static uint8_t           s_spi_ready;
static uint8_t           s_bus_hw;       /* 0 = soft (GPIO), 1 = HW SPI1    */

/* Soft-bus sweep knobs (the bit rate is critical for this module:
 * direct BSRR writes at 7-10 MHz produced an all-white panel). */
static uint8_t s_soft_pace = 0U;   /* 0 = fastest BSRR soft (~5-7 MHz) */   /* volatile-loop iterations per half-bit:
                                       * 1 ~= 2.5 MHz with BSRR writes
                                       * (0 = ~5 MHz, retest with fonts) */
static uint8_t s_soft_slew_high = 0U;

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
    __HAL_RCC_DMA2_CLK_ENABLE();        /* SPI1_TX DMA (raster bursts) */

    s_hspi.Instance               = SPI1;
    s_hspi.Init.Mode              = SPI_MODE_MASTER;
    s_hspi.Init.Direction         = SPI_DIRECTION_2LINES;     /* full duplex */
    s_hspi.Init.DataSize          = SPI_DATASIZE_8BIT;
    s_hspi.Init.CLKPolarity       = SPI_POLARITY_LOW;         /* mode 0 - as the working Arduino DIYables ILI9488 driver */
    s_hspi.Init.CLKPhase          = SPI_PHASE_1EDGE;
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

#if LCD_USE_DMA
    /* EXPERIMENTAL (not enabled by default): DMA bursts complete
     * (TCIF5 sets) but the panel shows nothing - under investigation.
     * DMA2 Stream5 channel 3 -> SPI1_TX, byte-wide, mem increment. */
    if (len >= 32U)
    {
        DMA2_Stream5->CR = 0U;
        while (DMA2_Stream5->CR & DMA_SxCR_EN) { }

        DMA2_Stream5->PAR  = (uint32_t)&SPI1->DR;
        DMA2_Stream5->M0AR = (uint32_t)buf;
        DMA2_Stream5->NDTR = len;
        DMA2_Stream5->CR = DMA_SxCR_CHSEL_0 | DMA_SxCR_CHSEL_1
                         | DMA_SxCR_MINC
                         | DMA_SxCR_DIR_0
                         | DMA_SxCR_PL_1
                         | DMA_SxCR_EN;

        SPI1->CR2 |= SPI_CR2_TXDMAEN;
        while (DMA2_Stream5->CR & DMA_SxCR_EN) { }
        SPI1->CR2 &= ~SPI_CR2_TXDMAEN;
    }
    else
#endif
    {
        for (; i < len; i++)
        {
            while ((SPI1->SR & SPI_SR_TXE) == 0U) { }
            *((__IO uint8_t *)&SPI1->DR) = buf[i];
        }
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
    g.Speed = s_soft_slew_high ? GPIO_SPEED_FREQ_HIGH : GPIO_SPEED_FREQ_LOW;
    g.Pull  = GPIO_NOPULL;
    g.Pin   = LCD_SCL_Pin | LCD_SDA_Pin;
    HAL_GPIO_Init(GPIOA, &g);
    /* idle SCL/SDA high; MISO (PA6) floats as input - soft is TX-only */
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
    g.Pin       = LCD_SCL_Pin | LCD_SDA_Pin | LCD_MISO_Pin;
    HAL_GPIO_Init(GPIOA, &g);

    spi_hw_init();
}

uint8_t LCD_BusIsHw(void)
{
    return s_bus_hw;
}

/* Active SPI1 baud in kHz (for the info page), e.g. 50000 = 50 MHz. */
/* Runtime SCK switch (speed sweep): write the BR field with SPE off.
 * `presc` is the SPI_BAUDRATEPRESCALER_x value (0=/2 .. 7=/256). */
void LCD_HwSetPrescaler(uint8_t presc)
{
    SPI_HW_Flush();
    __HAL_SPI_DISABLE(&s_hspi);
    SPI1->CR1 = (SPI1->CR1 & ~SPI_CR1_BR) | (presc & SPI_CR1_BR);
    __HAL_SPI_ENABLE(&s_hspi);
    s_hspi.Init.BaudRatePrescaler = presc & SPI_CR1_BR;
}

unsigned long LCD_HwSpiKHz(void)
{
    uint32_t div = 2U << ((s_hspi.Init.BaudRatePrescaler & SPI_CR1_BR) >>
                          SPI_CR1_BR_Pos);
    return (unsigned long)(HAL_RCC_GetPCLK2Freq() / div / 1000U);
}

/* ---------------- SOFT path: bit-bang PA5/PA7 ------------------------ */

void LCD_SoftSetPace(uint8_t iterations)
{
    s_soft_pace = iterations;
}

void LCD_SoftSetSlew(uint8_t high)
{
    GPIO_InitTypeDef g;

    s_soft_slew_high = high;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = high ? GPIO_SPEED_FREQ_HIGH : GPIO_SPEED_FREQ_LOW;
    g.Pin   = LCD_SCL_Pin | LCD_SDA_Pin;
    HAL_GPIO_Init(GPIOA, &g);
}

static void SendDataSPI(uint8_t dat)
{
    /* Direct BSRR/BRR writes with a pace knob. The earlier "white at
     * 7-10 MHz" finding was the 16-bit COLMOD bug (now fixed: 18-bit
     * pixels), not the wire - so pace 0 (fastest, ~8-12 MHz) is worth
     * retesting. pace 3 (~2 MHz) is the conservative default. */
    for (int i = 0; i < 8; i++)
    {
        GPIOA->BSRR = (1U << (5 + 16));      /* SCL low (BSRR reset half) */
        for (volatile int d = 0; d < s_soft_pace; d++) { }

        if ((dat & 0x80U) != 0U)
        {
            GPIOA->BSRR = (1U << 7);         /* SDA high */
        }
        else
        {
            GPIOA->BSRR = (1U << (7 + 16));  /* SDA low (BSRR reset half) */
        }
        for (volatile int d = 0; d < s_soft_pace; d++) { }

        GPIOA->BSRR = (1U << 5);             /* SCL high - panel samples */
        dat <<= 1;
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
    /* burst16 DISABLED for bring-up: the mid-burst DFF switch toggles
     * SCK while CS is low (nv3030b SPE-glitch class) - a prime suspect
     * for the garbled glyphs. Re-enable only if proven harmless. */
    s_burst16 = 0U;
    LCD_CS_CLR;
    LCD_RS_SET;
}
void LCD_EndData(void)
{
    s_burst16 = 0U;
    SPI_HW_Flush();
    LCD_CS_SET;
}

/* ---------------- read-back (HW SPI1 only) --------------------------- */

/* ILI9488 read cycles require a slower clock than writes. Keep MOSI
 * high-impedance during the response; the module has a separate SDO pin.
 *
 * The panel must have its SPI-read mode ARMED first: register 0xFB
 * (SPI read settings) = 0x80 | count, re-issued before each response
 * byte with count = 1, 2, ... ; 0xFB = 0x00 afterwards leaves read mode
 * (reference: Atmel SAM V71 FreeRTOS libboard ili9488.c, ReadReg24). */
uint8_t LCD_ReadBytes(uint8_t cmd, uint8_t *out, uint8_t nread)
{
    uint8_t rx[8];
    GPIO_InitTypeDef g;
    uint32_t saved_br;
    HAL_StatusTypeDef status;

    SPI_HW_Flush();
    if (s_bus_hw == 0U || out == NULL || nread == 0U || nread > 4U)
    {
        return 0U;
    }

    while ((SPI1->SR & SPI_SR_BSY) != 0U) { }
    while ((SPI1->SR & SPI_SR_RXNE) != 0U) { (void)SPI1->DR; }
    if ((SPI1->SR & SPI_SR_OVR) != 0U)
    {
        (void)SPI1->DR;
        (void)SPI1->SR;
    }

    saved_br = SPI1->CR1 & SPI_CR1_BR;
    __HAL_SPI_DISABLE(&s_hspi);
    SPI1->CR1 = (SPI1->CR1 & ~SPI_CR1_BR) | SPI_BAUDRATEPRESCALER_32;
    __HAL_SPI_ENABLE(&s_hspi);

    /* Arm the read mode: 0xFB = 0x80 | nread. */
    LCD_CS_CLR;
    LCD_RS_CLR;
    spi_send_now(0xFBU);
    LCD_RS_SET;
    spi_send_now((uint8_t)(0x80U | nread));
    LCD_CS_SET;

    /* Send the read command (DC low). */
    LCD_CS_CLR;
    LCD_RS_CLR;
    spi_send_now(cmd);
    LCD_RS_SET;

    /* Response: PA7 must stop driving; the panel drives its SDO line. */
    g.Pin   = LCD_SDA_Pin;
    g.Mode  = GPIO_MODE_INPUT;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &g);

    memset(rx, 0xFF, sizeof rx);
    status = HAL_SPI_TransmitReceive(&s_hspi, rx, rx, nread, 10U);
    while ((SPI1->SR & SPI_SR_BSY) != 0U) { }

    if (status == HAL_OK)
    {
        for (uint8_t i = 0U; i < nread; i++)
        {
            out[i] = rx[i];
        }
    }

    /* PA7 back to SPI1 MOSI before leaving read mode. */
    g.Pin   = LCD_SDA_Pin;
    g.Mode  = GPIO_MODE_AF_PP;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &g);

    LCD_CS_SET;

    /* Leave read mode: 0xFB = 0x00. */
    LCD_CS_CLR;
    LCD_RS_CLR;
    spi_send_now(0xFBU);
    LCD_RS_SET;
    spi_send_now(0x00U);
    LCD_CS_SET;

    /* Restore the write clock. */
    __HAL_SPI_DISABLE(&s_hspi);
    SPI1->CR1 = (SPI1->CR1 & ~SPI_CR1_BR) | saved_br;
    __HAL_SPI_ENABLE(&s_hspi);

    return (status == HAL_OK) ? 1U : 0U;
}
