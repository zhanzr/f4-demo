/*
  interface.c - low-level ILI9488 bus primitives (nucleo-f413 port).

  One wiring, two drive methods selected at runtime:
    SOFT: PA5/PA7 as plain GPIO outputs, bit-banged (DC pin selects
          command vs data).
    HW:   PA5/PA7 re-muxed to SPI1 AF5 (SCK/MOSI), master, 8-bit, mode 0
          (CPOL=0, CPHA=1EDGE - matching the working Arduino DIYables
          ILI9488 driver), MSB first, NSS soft. APB2 runs at 100 MHz
          natively on this board: prescaler /2 = 50 MHz SCK, the SPI1
          maximum and the verified default (25 MHz also renders clean;
          12.5 MHz garbles - a bad spot of the panel's internal clock
          divider window).

  Raster data in HW mode is rendered into one of two 4 KB buffers while
  DMA2 Stream5 channel 3 (SPI1_TX) streams the other one at full wire
  speed, so byte generation and the wire overlap. Commands and short
  tails stay on the polled path. Solid-color fills skip byte generation
  entirely (color pattern -> repeated DMA bursts).

    SCL = PA5, MOSI = PA7, MISO = PA6, DC = PD15, RES = PF12,
    CS = PD14 (GPIO).
*/

#include <stdint.h>
#include "lcd.h"
#include <string.h>
#include "stm32f4xx_hal.h"

/* Bytes of pixel data accumulated before one SPI1 DMA burst (HW only). */
#define SPI_TX_BUF_SIZE 4096U

/* Shortest transfer worth a DMA burst; below it the polled path wins. */
#define SPI_DMA_MIN_BYTES 32U

/* Repeating RGB666 color pattern used by solid fills (1024 pixels). */
#define SPI_PAT_BUF_SIZE 3072U

/* SPI1 baud prescaler: APB2 = 100 MHz natively on the F413 board clock
 * tree; /2 = 50 MHz SCK is the SPI1 maximum and the verified default. */
#ifndef LCD_SPI1_PRESC
#define LCD_SPI1_PRESC SPI_BAUDRATEPRESCALER_2
#endif

/* Two raster buffers: the CPU fills one while DMA streams the other. */
static uint8_t              s_tx_buf[2][SPI_TX_BUF_SIZE];
static uint8_t              s_pat_buf[SPI_PAT_BUF_SIZE];
static uint8_t              s_tx_cur;      /* buffer being filled         */
static uint16_t             s_tx_len;      /* bytes filled in it          */
static uint8_t              s_dma_busy;    /* a burst is in flight        */
static SPI_HandleTypeDef    s_hspi;
static uint8_t              s_spi_ready;
static uint8_t              s_bus_hw;      /* 0 = soft (GPIO), 1 = HW SPI1 */

/* Soft-bus sweep knobs (the bit rate is critical for this module). */
static uint8_t s_soft_pace = 0U;   /* volatile-loop iterations per half-bit */
static uint8_t s_soft_slew_high = 1U;

static void SendDataSPI(uint8_t dat);   /* soft bit-bang, defined below */

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
    __HAL_RCC_DMA2_CLK_ENABLE();        /* SPI1_TX raster bursts */

    s_hspi.Instance               = SPI1;
    s_hspi.Init.Mode              = SPI_MODE_MASTER;
    s_hspi.Init.Direction         = SPI_DIRECTION_2LINES;     /* full duplex */
    s_hspi.Init.DataSize          = SPI_DATASIZE_8BIT;
    s_hspi.Init.CLKPolarity       = SPI_POLARITY_LOW;         /* mode 0 */
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

/* Ensure the SPI is enabled: HAL transfers disable SPE when they close,
 * and the register-level paths below rely on SPE being set (with SPE=0
 * the TX buffer never drains and neither TXE nor DMA requests follow). */
static void spi_hw_enable(void)
{
    if ((SPI1->CR1 & SPI_CR1_SPE) == 0U)
    {
        SPI1->CR1 |= SPI_CR1_SPE;
    }
}

/* ---------------- SPI1_TX DMA (DMA2 Stream5 channel 3) --------------- */

static void dma_flags_clear(void)
{
    DMA2->HIFCR = (1UL << 6) | (1UL << 8) | (1UL << 9) |
                  (1UL << 10) | (1UL << 11);
}

/* Arm and start one burst. Waits for a previous burst to finish first,
 * so the buffer it reads is never rewritten under it. */
static void dma_tx_start(const uint8_t *buf, uint32_t len)
{
    if (s_dma_busy != 0U)
    {
        while ((DMA2_Stream5->CR & DMA_SxCR_EN) != 0U) { }
        while ((SPI1->SR & SPI_SR_BSY) != 0U) { }
        s_dma_busy = 0U;
    }

    DMA2_Stream5->CR &= ~DMA_SxCR_EN;
    while ((DMA2_Stream5->CR & DMA_SxCR_EN) != 0U) { }
    dma_flags_clear();

    DMA2_Stream5->PAR  = (uint32_t)&SPI1->DR;
    DMA2_Stream5->M0AR = (uint32_t)buf;
    DMA2_Stream5->NDTR = len;
    DMA2_Stream5->FCR  = 0U;                 /* direct mode, no FIFO */
    DMA2_Stream5->CR   = DMA_SxCR_CHSEL_0 | DMA_SxCR_CHSEL_1  /* ch 3  */
                       | DMA_SxCR_MINC                        /* mem++ */
                       | DMA_SxCR_DIR_0                       /* mem->p */
                       | DMA_SxCR_PL_0 | DMA_SxCR_PL_1;       /* v.high */

    SPI1->CR2 |= SPI_CR2_TXDMAEN;
    DMA2_Stream5->CR |= DMA_SxCR_EN;
    s_dma_busy = 1U;
}

/* Wait for the in-flight burst, then let the shifter drain. The RX side
 * is left alone here: full-duplex echoes overrun (harmlessly) during a
 * burst and are cleared once per burst group by SPI_HW_Flush. */
static void dma_tx_wait(void)
{
    if (s_dma_busy == 0U)
    {
        return;
    }
    while ((DMA2_Stream5->CR & DMA_SxCR_EN) != 0U) { }
    while ((SPI1->SR & SPI_SR_BSY) != 0U) { }
    SPI1->CR2 &= ~SPI_CR2_TXDMAEN;
    s_dma_busy = 0U;
}

/* Push a full buffer: start the burst, then flip to the other buffer so
 * the caller keeps filling while the wire is busy. */
static void spi_push_buf(void)
{
    uint8_t *p = s_tx_buf[s_tx_cur];
    uint16_t n = s_tx_len;

    s_tx_len = 0U;
    s_tx_cur ^= (uint8_t)1U;
    spi_hw_enable();
    dma_tx_start(p, n);
}

/* Small/blocking transmit (commands, tails): polled per-byte TXE writes. */
static void spi_hw_tx(uint8_t *buf, uint16_t len)
{
    uint16_t i;

    if (len == 0U)
    {
        return;
    }
    if (s_bus_hw == 0U)
    {
        for (i = 0U; i < len; i++)
        {
            SendDataSPI(buf[i]);
        }
        return;
    }

    if (len >= SPI_DMA_MIN_BYTES)
    {
        spi_hw_enable();
        dma_tx_start(buf, len);
        dma_tx_wait();
    }
    else
    {
        spi_hw_enable();
        for (i = 0U; i < len; i++)
        {
            while ((SPI1->SR & SPI_SR_TXE) == 0U) { }
            *((__IO uint8_t *)&SPI1->DR) = buf[i];
        }
        while ((SPI1->SR & SPI_SR_BSY) != 0U) { }
    }

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
        if (s_tx_len >= SPI_DMA_MIN_BYTES)
        {
            spi_push_buf();
        }
        else
        {
            uint8_t *p = s_tx_buf[s_tx_cur];
            uint16_t n = s_tx_len;

            dma_tx_wait();
            s_tx_len = 0U;
            spi_hw_tx(p, n);
        }
    }

    dma_tx_wait();

    while ((SPI1->SR & SPI_SR_RXNE) != 0U) { (void)SPI1->DR; }
    if ((SPI1->SR & SPI_SR_OVR) != 0U)
    {
        (void)SPI1->DR;
        (void)SPI1->SR;
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
    /* Direct BSRR/BRR writes with a pace knob. */
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
        (void)rx;
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
    s_tx_buf[s_tx_cur][s_tx_len++] = dat;
    if (s_tx_len >= SPI_TX_BUF_SIZE)
    {
        spi_push_buf();
    }
}

/* Write a register/command: DC low. */
void WriteComm(uint16_t data)
{
    SPI_HW_Flush();
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

/* Push one RGB565 pixel as the panel's 18-bit RGB666 frame (3 bytes).
 * One call per pixel instead of three keeps raster loops cheap; the
 * buffer is checked once per pixel so a flush never splits a frame. */
void LCD_WritePixelRGB565(uint16_t c565)
{
    uint8_t r = (uint8_t)(((c565 >> 8) & 0xF8U) | ((c565 >> 13) & 0x07U));
    uint8_t g = (uint8_t)(((c565 >> 3) & 0xFCU) | ((c565 >> 9) & 0x03U));
    uint8_t b = (uint8_t)((c565 << 3) & 0xF8U);

    if (s_bus_hw == 0U)
    {
        SendDataSPI(r);
        SendDataSPI(g);
        SendDataSPI(b);
        return;
    }

    if (s_tx_len > (SPI_TX_BUF_SIZE - 3U))
    {
        spi_push_buf();
    }

    {
        uint8_t *p = &s_tx_buf[s_tx_cur][s_tx_len];
        p[0] = r;
        p[1] = g;
        p[2] = b;
        s_tx_len = (uint16_t)(s_tx_len + 3U);
    }
}

/* Stream `npix` pixels of one color: repeated DMA bursts of a prepared
 * color pattern on the HW bus, plain bit-bang on the soft bus. */
void LCD_StreamColor565(uint16_t c565, uint32_t npix)
{
    uint8_t  r = (uint8_t)(((c565 >> 8) & 0xF8U) | ((c565 >> 13) & 0x07U));
    uint8_t  g = (uint8_t)(((c565 >> 3) & 0xFCU) | ((c565 >> 9) & 0x03U));
    uint8_t  b = (uint8_t)((c565 << 3) & 0xF8U);
    uint32_t total;
    uint32_t len;

    if (npix == 0U)
    {
        return;
    }

    if (s_bus_hw == 0U)
    {
        while (npix-- != 0U)
        {
            SendDataSPI(r);
            SendDataSPI(g);
            SendDataSPI(b);
        }
        return;
    }

    total = npix * 3U;
    len   = (total < (uint32_t)SPI_PAT_BUF_SIZE) ? total
                                                 : (uint32_t)SPI_PAT_BUF_SIZE;
    len  -= len % 3U;

    for (uint32_t i = 0U; i < len; i += 3U)
    {
        s_pat_buf[i]      = r;
        s_pat_buf[i + 1U] = g;
        s_pat_buf[i + 2U] = b;
    }

    spi_hw_enable();
    while (total != 0U)
    {
        uint32_t n = (total < len) ? total : len;

        dma_tx_start(s_pat_buf, n);
        dma_tx_wait();
        total -= n;
    }

    while ((SPI1->SR & SPI_SR_RXNE) != 0U) { (void)SPI1->DR; }
    if ((SPI1->SR & SPI_SR_OVR) != 0U)
    {
        (void)SPI1->DR;
        (void)SPI1->SR;
    }
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
