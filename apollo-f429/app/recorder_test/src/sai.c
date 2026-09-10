/*
 * sai.c - SAI1 audio driver (clean port of the vendored ALIENTEK Apollo 46
 * recorder example). Block A = master TX/DMA2-Stream3, Block B = slave
 * RX/DMA2-Stream5, PLLI2S clock, PE2-6 AF6. `sai_tx_callback`/`sai_rx_callback`
 * are invoked from the DMA TC IRQ handlers and are provided by the app.
 */
#include "sai.h"
#include "sys_compat.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_rcc_ex.h"

SAI_HandleTypeDef SAI1A_Handler;
SAI_HandleTypeDef SAI1B_Handler;
DMA_HandleTypeDef SAI1_TXDMA_Handler;
DMA_HandleTypeDef SAI1_RXDMA_Handler;
void (*sai_tx_callback)(void);
void (*sai_rx_callback)(void);

/* Sample-rate table: {Fs/10, PLLI2SN, PLLI2SQ, PLLI2SDivQ, MCKDIV} for
 * HSE=25MHz, PLLM=25 (VCO input 1 MHz). 44.1kHz = {441, 429, 2, 18, 0}. */
static const uint16_t SAI_PSC_TBL[][5] = {
    { 800,  344, 7,  0, 12 },
    { 1102, 429, 2, 18,  2 },
    { 1600, 344, 7,  0,  6 },
    { 2205, 429, 2, 18,  1 },
    { 3200, 344, 7,  0,  3 },
    { 4410, 429, 2, 18,  0 },
    { 4800, 344, 7,  0,  2 },
    { 8820, 271, 2,  2,  1 },
    { 9600, 344, 7,  0,  1 },
    { 17640, 271, 2, 2,  0 },
    { 19200, 344, 7, 0,  0 },
};

/* Reset the SAI1 peripheral (APB2 reset). Clears any stale SR flags (e.g.
 * OVRUDR/FREQ) left from a previous record/play cycle so both blocks start
 * clean on the next pass. */
void SAI1_Reset(void)
{
    __HAL_RCC_SAI1_CLK_ENABLE();
    RCC->APB2RSTR |= RCC_APB2RSTR_SAI1RST;
    __asm volatile ("dmb");
    RCC->APB2RSTR &= ~RCC_APB2RSTR_SAI1RST;
    __asm volatile ("dmb");
    HAL_Delay(1);
}

void SAIA_Init(uint32_t mode, uint32_t cpol, uint32_t datalen)
{
    HAL_SAI_DeInit(&SAI1A_Handler);
    SAI1A_Handler.Instance = SAI1_Block_A;
    SAI1A_Handler.Init.AudioMode = mode;
    SAI1A_Handler.Init.Synchro = SAI_ASYNCHRONOUS;
    SAI1A_Handler.Init.OutputDrive = SAI_OUTPUTDRIVE_ENABLE;
    SAI1A_Handler.Init.NoDivider = SAI_MASTERDIVIDER_ENABLE;
    SAI1A_Handler.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_1QF;
    SAI1A_Handler.Init.ClockSource = SAI_CLKSOURCE_PLLI2S;
    SAI1A_Handler.Init.MonoStereoMode = SAI_STEREOMODE;
    SAI1A_Handler.Init.Protocol = SAI_FREE_PROTOCOL;
    SAI1A_Handler.Init.DataSize = datalen;
    SAI1A_Handler.Init.FirstBit = SAI_FIRSTBIT_MSB;
    SAI1A_Handler.Init.ClockStrobing = cpol;

    SAI1A_Handler.FrameInit.FrameLength = 64;
    SAI1A_Handler.FrameInit.ActiveFrameLength = 32;
    SAI1A_Handler.FrameInit.FSDefinition = SAI_FS_CHANNEL_IDENTIFICATION;
    SAI1A_Handler.FrameInit.FSPolarity = SAI_FS_ACTIVE_LOW;
    SAI1A_Handler.FrameInit.FSOffset = SAI_FS_BEFOREFIRSTBIT;

    SAI1A_Handler.SlotInit.FirstBitOffset = 0;
    SAI1A_Handler.SlotInit.SlotSize = SAI_SLOTSIZE_32B;
    SAI1A_Handler.SlotInit.SlotNumber = 2;
    SAI1A_Handler.SlotInit.SlotActive = SAI_SLOTACTIVE_0 | SAI_SLOTACTIVE_1;

    HAL_SAI_Init(&SAI1A_Handler);
    __HAL_SAI_ENABLE(&SAI1A_Handler);
}

void SAIB_Init(uint32_t mode, uint32_t cpol, uint32_t datalen)
{
    HAL_SAI_DeInit(&SAI1B_Handler);
    SAI1B_Handler.Instance = SAI1_Block_B;
    SAI1B_Handler.Init.AudioMode = mode;
    SAI1B_Handler.Init.Synchro = SAI_SYNCHRONOUS;
    SAI1B_Handler.Init.OutputDrive = SAI_OUTPUTDRIVE_ENABLE;
    SAI1B_Handler.Init.NoDivider = SAI_MASTERDIVIDER_ENABLE;
    SAI1B_Handler.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_1QF;
    SAI1B_Handler.Init.ClockSource = SAI_CLKSOURCE_PLLI2S;
    SAI1B_Handler.Init.MonoStereoMode = SAI_STEREOMODE;
    SAI1B_Handler.Init.Protocol = SAI_FREE_PROTOCOL;
    SAI1B_Handler.Init.DataSize = datalen;
    SAI1B_Handler.Init.FirstBit = SAI_FIRSTBIT_MSB;
    SAI1B_Handler.Init.ClockStrobing = cpol;

    SAI1B_Handler.FrameInit.FrameLength = 64;
    SAI1B_Handler.FrameInit.ActiveFrameLength = 32;
    SAI1B_Handler.FrameInit.FSDefinition = SAI_FS_CHANNEL_IDENTIFICATION;
    SAI1B_Handler.FrameInit.FSPolarity = SAI_FS_ACTIVE_LOW;
    SAI1B_Handler.FrameInit.FSOffset = SAI_FS_BEFOREFIRSTBIT;

    SAI1B_Handler.SlotInit.FirstBitOffset = 0;
    SAI1B_Handler.SlotInit.SlotSize = SAI_SLOTSIZE_32B;
    SAI1B_Handler.SlotInit.SlotNumber = 2;
    SAI1B_Handler.SlotInit.SlotActive = SAI_SLOTACTIVE_0 | SAI_SLOTACTIVE_1;

    HAL_SAI_Init(&SAI1B_Handler);
    SAIB_DMA_Enable();
    __HAL_SAI_ENABLE(&SAI1B_Handler);
}

void HAL_SAI_MspInit(SAI_HandleTypeDef *hsai)
{
    GPIO_InitTypeDef gpio = {0};
    (void)hsai;

    __HAL_RCC_SAI1_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_HIGH;
    gpio.Alternate = GPIO_AF6_SAI1;
    HAL_GPIO_Init(GPIOE, &gpio);
}

void SAIA_DMA_Enable(void)
{
    SAI1_Block_A->CR1 |= 1U << 17;
}

void SAIB_DMA_Enable(void)
{
    SAI1_Block_B->CR1 |= 1U << 17;
}

uint8_t SAIA_SampleRate_Set(uint32_t samplerate)
{
    uint8_t i;
    RCC_PeriphCLKInitTypeDef clk = {0};

    for (i = 0; i < (sizeof(SAI_PSC_TBL) / 10); i++)
    {
        if ((samplerate / 10U) == SAI_PSC_TBL[i][0]) { break; }
    }
    if (i == (sizeof(SAI_PSC_TBL) / 10)) { return 1; }

    clk.PeriphClockSelection = RCC_PERIPHCLK_SAI_PLLI2S;
    clk.PLLI2S.PLLI2SN = SAI_PSC_TBL[i][1];
    clk.PLLI2S.PLLI2SQ = SAI_PSC_TBL[i][2];
    clk.PLLI2SDivQ = (uint32_t)SAI_PSC_TBL[i][3] + 1;
    HAL_RCCEx_PeriphCLKConfig(&clk);

    __HAL_RCC_SAI_BLOCKACLKSOURCE_CONFIG(RCC_SAIACLKSOURCE_PLLI2S);

    __HAL_SAI_DISABLE(&SAI1A_Handler);
    SAI1A_Handler.Init.AudioFrequency = samplerate;
    HAL_SAI_Init(&SAI1A_Handler);
    SAIA_DMA_Enable();
    __HAL_SAI_ENABLE(&SAI1A_Handler);
    return 0;
}

void SAIA_TX_DMA_Init(uint8_t *buf0, uint8_t *buf1, uint16_t num, uint8_t width)
{
    uint32_t memw = DMA_MDATAALIGN_HALFWORD;
    uint32_t perw = DMA_PDATAALIGN_HALFWORD;
    if (width == 0) { memw = DMA_MDATAALIGN_BYTE;   perw = DMA_PDATAALIGN_BYTE; }
    if (width == 2) { memw = DMA_MDATAALIGN_WORD;    perw = DMA_PDATAALIGN_WORD; }

    __HAL_RCC_DMA2_CLK_ENABLE();
    __HAL_LINKDMA(&SAI1A_Handler, hdmatx, SAI1_TXDMA_Handler);
    SAI1_TXDMA_Handler.Instance = DMA2_Stream3;
    SAI1_TXDMA_Handler.Init.Channel = DMA_CHANNEL_0;
    SAI1_TXDMA_Handler.Init.Direction = DMA_MEMORY_TO_PERIPH;
    SAI1_TXDMA_Handler.Init.PeriphInc = DMA_PINC_DISABLE;
    SAI1_TXDMA_Handler.Init.MemInc = DMA_MINC_ENABLE;
    SAI1_TXDMA_Handler.Init.PeriphDataAlignment = perw;
    SAI1_TXDMA_Handler.Init.MemDataAlignment = memw;
    SAI1_TXDMA_Handler.Init.Mode = DMA_CIRCULAR;
    SAI1_TXDMA_Handler.Init.Priority = DMA_PRIORITY_HIGH;
    SAI1_TXDMA_Handler.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    SAI1_TXDMA_Handler.Init.MemBurst = DMA_MBURST_SINGLE;
    SAI1_TXDMA_Handler.Init.PeriphBurst = DMA_PBURST_SINGLE;
    HAL_DMA_DeInit(&SAI1_TXDMA_Handler);
    HAL_DMA_Init(&SAI1_TXDMA_Handler);

    HAL_DMAEx_MultiBufferStart(&SAI1_TXDMA_Handler, (uint32_t)buf0,
                               (uint32_t)&SAI1_Block_A->DR, (uint32_t)buf1, num);
    __HAL_DMA_DISABLE(&SAI1_TXDMA_Handler);
    delay_us(10);
    __HAL_DMA_ENABLE_IT(&SAI1_TXDMA_Handler, DMA_IT_TC);
    __HAL_DMA_CLEAR_FLAG(&SAI1_TXDMA_Handler, DMA_FLAG_TCIF3_7);
    HAL_NVIC_SetPriority(DMA2_Stream3_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream3_IRQn);
}

void SAIA_RX_DMA_Init(uint8_t *buf0, uint8_t *buf1, uint16_t num, uint8_t width)
{
    uint32_t memw = DMA_MDATAALIGN_HALFWORD;
    uint32_t perw = DMA_PDATAALIGN_HALFWORD;
    if (width == 0) { memw = DMA_MDATAALIGN_BYTE;   perw = DMA_PDATAALIGN_BYTE; }
    if (width == 2) { memw = DMA_MDATAALIGN_WORD;    perw = DMA_PDATAALIGN_WORD; }

    __HAL_RCC_DMA2_CLK_ENABLE();
    __HAL_LINKDMA(&SAI1B_Handler, hdmarx, SAI1_RXDMA_Handler);
    SAI1_RXDMA_Handler.Instance = DMA2_Stream5;
    SAI1_RXDMA_Handler.Init.Channel = DMA_CHANNEL_0;
    SAI1_RXDMA_Handler.Init.Direction = DMA_PERIPH_TO_MEMORY;
    SAI1_RXDMA_Handler.Init.PeriphInc = DMA_PINC_DISABLE;
    SAI1_RXDMA_Handler.Init.MemInc = DMA_MINC_ENABLE;
    SAI1_RXDMA_Handler.Init.PeriphDataAlignment = perw;
    SAI1_RXDMA_Handler.Init.MemDataAlignment = memw;
    SAI1_RXDMA_Handler.Init.Mode = DMA_CIRCULAR;
    SAI1_RXDMA_Handler.Init.Priority = DMA_PRIORITY_MEDIUM;
    SAI1_RXDMA_Handler.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    SAI1_RXDMA_Handler.Init.MemBurst = DMA_MBURST_SINGLE;
    SAI1_RXDMA_Handler.Init.PeriphBurst = DMA_PBURST_SINGLE;
    HAL_DMA_DeInit(&SAI1_RXDMA_Handler);
    HAL_DMA_Init(&SAI1_RXDMA_Handler);

    HAL_DMAEx_MultiBufferStart(&SAI1_RXDMA_Handler, (uint32_t)&SAI1_Block_B->DR,
                               (uint32_t)buf0, (uint32_t)buf1, num);
    __HAL_DMA_DISABLE(&SAI1_RXDMA_Handler);
    delay_us(10);
    __HAL_DMA_CLEAR_FLAG(&SAI1_RXDMA_Handler, DMA_FLAG_TCIF1_5);
    __HAL_DMA_ENABLE_IT(&SAI1_RXDMA_Handler, DMA_IT_TC);
    HAL_NVIC_SetPriority(DMA2_Stream5_IRQn, 0, 1);
    HAL_NVIC_EnableIRQ(DMA2_Stream5_IRQn);
}

void DMA2_Stream3_IRQHandler(void)
{
    if (__HAL_DMA_GET_FLAG(&SAI1_TXDMA_Handler, DMA_FLAG_TCIF3_7) != RESET)
    {
        __HAL_DMA_CLEAR_FLAG(&SAI1_TXDMA_Handler, DMA_FLAG_TCIF3_7);
        if (sai_tx_callback) { sai_tx_callback(); }
    }
}

void DMA2_Stream5_IRQHandler(void)
{
    if (__HAL_DMA_GET_FLAG(&SAI1_RXDMA_Handler, DMA_FLAG_TCIF1_5) != RESET)
    {
        __HAL_DMA_CLEAR_FLAG(&SAI1_RXDMA_Handler, DMA_FLAG_TCIF1_5);
        if (sai_rx_callback) { sai_rx_callback(); }
    }
}

void SAI_Play_Start(void) { __HAL_DMA_ENABLE(&SAI1_TXDMA_Handler); }
void SAI_Play_Stop(void)  { __HAL_DMA_DISABLE(&SAI1_TXDMA_Handler); }
void SAI_Rec_Start(void)  { __HAL_DMA_ENABLE(&SAI1_RXDMA_Handler); }
void SAI_Rec_Stop(void)   { __HAL_DMA_DISABLE(&SAI1_RXDMA_Handler); }

/* The HAL DMA stays BUSY after a circular transfer is disabled; re-initialising
 * on the next cycle would silently fail (HAL_DMA_DeInit returns HAL_BUSY). Force
 * both SAI DMA streams back to READY before re-arming. */
void SAI_DMA_ResetAll(void)
{
    SAI1_TXDMA_Handler.State = HAL_DMA_STATE_READY;
    SAI1_TXDMA_Handler.ErrorCode = HAL_DMA_ERROR_NONE;
    SAI1_RXDMA_Handler.State = HAL_DMA_STATE_READY;
    SAI1_RXDMA_Handler.ErrorCode = HAL_DMA_ERROR_NONE;
}