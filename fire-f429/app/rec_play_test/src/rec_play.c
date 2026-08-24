/*
 * Record/play engine for the fire-f429 board - WM8978 codec on I2S2
 * (full-duplex), with the recorded PCM kept in a RAM buffer.
 *
 * Simplified port of the Wildfire F429 "37-I2S_audio / I2S_record_play"
 * example: no FatFs, no SD card, no WAV headers. The stereo 16-bit PCM
 * stream is captured into one big SDRAM buffer and played back from it.
 *
 * Hardware (identical to the vendor example):
 *   I2S2 (master TX):   WS/LRC=PB12, BCLK=PD3, DACDAT(I2S2_SD)=PI3, MCLK=PC6
 *   I2S2ext (slave RX): ADCDAT(I2S2ext_SD)=PC2
 *   Codec control:      I2C1 PB6/PB7 (wm8978.c)
 *   DMA: TX = DMA1 Stream4 Ch0, RX = DMA1 Stream3 Ch3 (both double buffered)
 *
 * Engine: both streams run as 20 ms chunks in a DMA double buffer out of
 * internal SRAM (.sram_dma - DMA buffers must not live in the external
 * SDRAM, per this repo's convention). On every transfer-complete the ISR
 * copies the just-filled chunk into the big SDRAM recording buffer (record)
 * or prefetches the next chunk from it (play). One 7 KB copy every 20 ms is
 * trivial for the 180 MHz core.
 *
 * Full-duplex quirk: I2S2ext only shifts while the I2S2 master transmits,
 * so while recording the TX DMA streams a zero-filled dummy chunk to keep
 * the master (and thereby I2S2ext) clocked.
 */

#include "rec_play.h"
#include "board.h"
#include "wm8978.h"
#include <string.h>

/* --- Format / size constants ---------------------------------------------- */

#define AUDIO_FREQ        I2S_AUDIOFREQ_44K   /* 44100 Hz          */
#define REC_SECONDS       30U                 /* buffer capacity   */
#define CHANNELS          2U                  /* stereo            */

/* u16 frames per 20 ms stereo chunk @ 44.1 kHz: 44100*0.02*2 = 1764. */
#define CHUNK_SAMPLES     1764U
#define TOTAL_SAMPLES     (44100U * REC_SECONDS * CHANNELS)  /* u16, stereo */
#define TOTAL_CHUNKS      1500U               /* 50 chunks/s * 30 s */

/* DMA working chunks (double-buffer ping-pong). DMA1 cannot reach the FMC
 * SDRAM, so every 20 ms chunk is captured into internal-SRAM chunks and then
 * copied into (record) / from (play) the big SDRAM buffer. */
static uint16_t chunk0[CHUNK_SAMPLES] __attribute__((section(".sram_dma"), used));
static uint16_t chunk1[CHUNK_SAMPLES] __attribute__((section(".sram_dma"), used));

/* Digital silence streamed to the DAC while recording, keeping the I2S2
 * master (and thus I2S2ext) clocked. Also internal SRAM for DMA. */
static uint16_t dummy_left[CHUNK_SAMPLES / 2U]  __attribute__((section(".sram_dma"), used));
static uint16_t dummy_right[CHUNK_SAMPLES / 2U] __attribute__((section(".sram_dma"), used));

/* The recording itself (~5.3 MB): .bss, which the shared app linker script
 * (stm32f429_sdram.ld) places in the onboard SDRAM. */
static uint16_t pcm_buffer[TOTAL_SAMPLES];

/* --- Handles / state ------------------------------------------------------- */

typedef enum
{
    RP_IDLE = 0,
    RP_RECORDING,
    RP_PLAYING,
} RP_State;

static I2S_HandleTypeDef hi2s2;           /* SPI2, master TX */
static DMA_HandleTypeDef hdma_tx;         /* DMA1 Stream4 Ch0 */
static DMA_HandleTypeDef hdma_rx;         /* DMA1 Stream3 Ch3 */

static volatile RP_State rp_state       = RP_IDLE;
static volatile uint32_t rp_chunk       = 0;  /* record: chunks stored so far      */
static volatile uint32_t rp_total       = 0;  /* chunks this session will handle   */
static volatile uint32_t rp_play_loaded = 0;  /* play: next chunk index to load    */
static volatile uint32_t rp_play_aired  = 0;  /* play: chunks finished airing      */
static volatile uint8_t  rp_record_done = 0;  /* recording reached its end         */
static volatile uint8_t  rp_play_done   = 0;  /* playback drained                  */

/* --- PLLI2S: 44.1 kHz family -------------------------------------------------
 * HSE 25 MHz / PLLM(25) = 1 MHz; VCO = 1 MHz * 271 = 271 MHz;
 * I2SCLK = 271 / 6 = 45.17 MHz -> MCLK = 44100 * 256 (exact).
 * (HAL_RCCEx_PeriphCLKConfig disables, reconfigures and re-enables PLLI2S
 * and waits for it to become ready.) */
static void I2SPLL_Config(void)
{
    RCC_PeriphCLKInitTypeDef cfg = {0};

    cfg.PeriphClockSelection = RCC_PERIPHCLK_I2S;
    cfg.PLLI2S.PLLI2SN       = 271U;
    cfg.PLLI2S.PLLI2SR       = 6U;
    if (HAL_RCCEx_PeriphCLKConfig(&cfg) != HAL_OK)
    {
        Error_Handler();
    }
}

/* --- GPIO / I2S / DMA init -------------------------------------------------- */

static void I2S_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();   /* PB12 WS   */
    __HAL_RCC_GPIOD_CLK_ENABLE();   /* PD3  BCLK */
    __HAL_RCC_GPIOC_CLK_ENABLE();   /* PC2 ADCDAT, PC6 MCLK */
    __HAL_RCC_GPIOI_CLK_ENABLE();   /* PI3 DACDAT */

    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    gpio.Pin       = GPIO_PIN_12;
    gpio.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOB, &gpio);

    gpio.Pin       = GPIO_PIN_3;
    gpio.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOD, &gpio);

    gpio.Pin       = GPIO_PIN_2;
    gpio.Alternate = GPIO_AF6_I2S2ext;
    HAL_GPIO_Init(GPIOC, &gpio);

    gpio.Pin       = GPIO_PIN_6;
    gpio.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOC, &gpio);

    gpio.Pin       = GPIO_PIN_3;
    gpio.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOI, &gpio);
}

/* DMA1 Stream4 Ch0: SPI2_TX (memory -> SPI2 DR), double buffered. */
static void TX_DMA_Init(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();

    hdma_tx.Instance                 = DMA1_Stream4;
    hdma_tx.Init.Channel             = DMA_CHANNEL_0;
    hdma_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    hdma_tx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_tx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    hdma_tx.Init.Mode                = DMA_CIRCULAR;
    hdma_tx.Init.Priority            = DMA_PRIORITY_HIGH;
    hdma_tx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&hdma_tx);

    __HAL_LINKDMA(&hi2s2, hdmatx, hdma_tx);

    HAL_NVIC_SetPriority(DMA1_Stream4_IRQn, 1U, 0U);
    HAL_NVIC_EnableIRQ(DMA1_Stream4_IRQn);
}

/* DMA1 Stream3 Ch3: I2S2ext_RX (I2S2ext DR -> memory), double buffered. */
static void RX_DMA_Init(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();

    hdma_rx.Instance                 = DMA1_Stream3;
    hdma_rx.Init.Channel             = DMA_CHANNEL_3;
    hdma_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_rx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    hdma_rx.Init.Mode                = DMA_CIRCULAR;
    hdma_rx.Init.Priority            = DMA_PRIORITY_HIGH;
    hdma_rx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&hdma_rx);

    /* NOTE: this stream serves the extension (I2S2ext) even though it is
     * linked to the I2S2 handle; it is started with the I2S2ext->DR address. */
    __HAL_LINKDMA(&hi2s2, hdmarx, hdma_rx);

    HAL_NVIC_SetPriority(DMA1_Stream3_IRQn, 1U, 0U);
    HAL_NVIC_EnableIRQ(DMA1_Stream3_IRQn);
}

/* I2S2 (master TX) + I2S2ext (slave RX): Philips, 16-bit, 44.1 kHz. */
static void I2S_Config(void)
{
    __HAL_RCC_SPI2_CLK_ENABLE();

    hi2s2.Instance            = SPI2;
    hi2s2.Init.Mode           = I2S_MODE_MASTER_TX;
    hi2s2.Init.Standard       = I2S_STANDARD_PHILIPS;
    hi2s2.Init.DataFormat     = I2S_DATAFORMAT_16B;
    hi2s2.Init.MCLKOutput     = I2S_MCLKOUTPUT_ENABLE;
    hi2s2.Init.AudioFreq      = AUDIO_FREQ;
    hi2s2.Init.CPOL           = I2S_CPOL_LOW;
    hi2s2.Init.ClockSource    = RCC_I2SCLKSOURCE_PLLI2S;
    hi2s2.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_ENABLE;   /* inits I2S2ext */
    HAL_I2S_Init(&hi2s2);
}

int RecPlay_Init(void)
{
    memset(dummy_left,  0, sizeof(dummy_left));
    memset(dummy_right, 0, sizeof(dummy_right));
    I2SPLL_Config();
    I2S_GPIO_Init();
    I2S_Config();
    TX_DMA_Init();
    RX_DMA_Init();
    return 0;
}

/* --- Engine stop ------------------------------------------------------------ */

/* Abort DMA + disable I2S. Returns chunks completed. Safe from ISR context:
 * stops the streams by register writes only (HAL_DMA_Abort polls HAL_GetTick
 * for its timeout, which can never expire inside a DMA ISR because SysTick
 * cannot preempt it). */
static uint32_t Engine_Stop(void)
{
    uint32_t done;

    CLEAR_BIT(SPI2->CR2, SPI_CR2_TXDMAEN);
    CLEAR_BIT(I2S2ext->CR2, SPI_CR2_RXDMAEN);

    /* Kill the stream interrupts, disable the streams and clear flags.
     * EN only falls after the in-flight single transfer completes. */
    CLEAR_BIT(hdma_tx.Instance->CR, DMA_IT_TC | DMA_IT_TE | DMA_IT_DME | DMA_IT_HT);
    CLEAR_BIT(hdma_tx.Instance->FCR, DMA_IT_FE);
    CLEAR_BIT(hdma_rx.Instance->CR, DMA_IT_TC | DMA_IT_TE | DMA_IT_DME | DMA_IT_HT);
    CLEAR_BIT(hdma_rx.Instance->FCR, DMA_IT_FE);
    __HAL_DMA_DISABLE(&hdma_tx);
    __HAL_DMA_DISABLE(&hdma_rx);
    __HAL_DMA_CLEAR_FLAG(&hdma_tx, DMA_FLAG_TCIF0_4 | DMA_FLAG_HTIF0_4 |
                                   DMA_FLAG_TEIF0_4  | DMA_FLAG_FEIF0_4 |
                                   DMA_FLAG_DMEIF0_4);
    __HAL_DMA_CLEAR_FLAG(&hdma_rx, DMA_FLAG_TCIF3_7 | DMA_FLAG_HTIF3_7 |
                                   DMA_FLAG_TEIF3_7  | DMA_FLAG_FEIF3_7 |
                                   DMA_FLAG_DMEIF3_7);

    /* Mark the HAL handles READY so the next MultiBufferStart succeeds. */
    hdma_tx.State = HAL_DMA_STATE_READY;
    hdma_rx.State = HAL_DMA_STATE_READY;
    __HAL_UNLOCK(&hdma_tx);
    __HAL_UNLOCK(&hdma_rx);

    __HAL_I2S_DISABLE(&hi2s2);
    __HAL_I2SEXT_DISABLE(&hi2s2);

    done     = rp_chunk;
    rp_state = RP_IDLE;
    return done;
}

/* --- Record ------------------------------------------------------------------ */

/* Start recording up to 30 s of stereo 16-bit PCM into the SDRAM buffer.
 * Returns immediately; the DMA callbacks walk the buffer chunk by chunk. */
void RecPlay_StartRecord(void)
{
    /* Codec: MICs -> ADC -> I2S; earphones monitor the analogue MIC mix. */
    WM8978_Reset();
    WM8978_CfgAudioPath(MIC_LEFT_ON | MIC_RIGHT_ON | ADC_ON,
                        EAR_LEFT_ON | EAR_RIGHT_ON);
    WM8978_SetMicGain(50U);
    WM8978_CfgAudioIF();

    rp_chunk       = 0;
    rp_total       = TOTAL_CHUNKS;
    rp_record_done = 0;

    /* TX: stream digital silence (two half-chunks ping-ponging) so the I2S2
     * master keeps generating BCLK/WS for the I2S2ext receiver. */
    hdma_tx.XferCpltCallback   = RecPlay_DMA_Silence;
    hdma_tx.XferM1CpltCallback = RecPlay_DMA_Silence;
    hdma_tx.XferErrorCallback  = RecPlay_DMA_Error;
    HAL_DMAEx_MultiBufferStart_IT(&hdma_tx,
                                  (uint32_t)dummy_left,
                                  (uint32_t)&SPI2->DR,
                                  (uint32_t)dummy_right,
                                  CHUNK_SAMPLES / 2U);

    /* RX: the first two 20 ms chunks land in chunk0/chunk1. */
    hdma_rx.XferCpltCallback   = RecPlay_DMA_Callback;
    hdma_rx.XferM1CpltCallback = RecPlay_DMA_Callback;
    hdma_rx.XferErrorCallback  = RecPlay_DMA_Error;
    HAL_DMAEx_MultiBufferStart_IT(&hdma_rx,
                                  (uint32_t)&I2S2ext->DR,
                                  (uint32_t)chunk0,
                                  (uint32_t)chunk1,
                                  CHUNK_SAMPLES);

    SET_BIT(SPI2->CR2, SPI_CR2_TXDMAEN);
    SET_BIT(I2S2ext->CR2, SPI_CR2_RXDMAEN);

    /* RM0090: in full-duplex slave mode, enable I2Sext BEFORE the master. */
    __HAL_I2SEXT_ENABLE(&hi2s2);
    __HAL_I2S_ENABLE(&hi2s2);

    rp_state = RP_RECORDING;
}

/* Stop an in-progress recording (no-op if already stopped); returns the
 * number of chunks actually captured. Always puts the codec back to a
 * quiet state and clears the done flags (main calls this after noticing
 * either the record or the play completion, making them one-shot). */
uint32_t RecPlay_StopRecord(void)
{
    uint32_t chunks = rp_chunk;

    if (rp_state == RP_RECORDING)
    {
        chunks = Engine_Stop();
    }
    rp_record_done = 0;
    rp_play_done   = 0;
    WM8978_Reset();
    return chunks;
}

/* --- Play -------------------------------------------------------------------- */

/* Start playback of the recorded buffer (chunks chunks). */
void RecPlay_StartPlay(uint32_t chunks)
{
    if (chunks == 0U) { return; }

    /* Codec: I2S DAC -> earphone. */
    WM8978_Reset();
    WM8978_CfgAudioPath(DAC_ON, EAR_LEFT_ON | EAR_RIGHT_ON);
    WM8978_SetOUT1Volume(40U);
    WM8978_CfgAudioIF();

    rp_total       = chunks;
    rp_play_loaded = 0;
    rp_play_aired  = 0;
    rp_play_done   = 0;

    /* Preload the ping-pong chunks with the first (up to) two chunks. */
    if (chunks >= 1U)
    {
        memcpy(chunk0, &pcm_buffer[0], CHUNK_SAMPLES * 2U);
        rp_play_loaded = 1U;
    }
    if (chunks >= 2U)
    {
        memcpy(chunk1, &pcm_buffer[CHUNK_SAMPLES], CHUNK_SAMPLES * 2U);
        rp_play_loaded = 2U;
    }
    if (rp_play_loaded == 1U)
    {
        memset(chunk1, 0, sizeof(chunk1));   /* single chunk: air silence   */
    }

    hdma_tx.XferCpltCallback   = RecPlay_DMA_Prefetch;
    hdma_tx.XferM1CpltCallback = RecPlay_DMA_Prefetch;
    hdma_tx.XferErrorCallback  = RecPlay_DMA_Error;
    HAL_DMAEx_MultiBufferStart_IT(&hdma_tx,
                                  (uint32_t)chunk0,
                                  (uint32_t)&SPI2->DR,
                                  (uint32_t)chunk1,
                                  CHUNK_SAMPLES);

    SET_BIT(SPI2->CR2, SPI_CR2_TXDMAEN);

    __HAL_I2SEXT_ENABLE(&hi2s2);
    __HAL_I2S_ENABLE(&hi2s2);

    rp_state = RP_PLAYING;
}

int RecPlay_IsPlaying(void)
{
    return (rp_state == RP_PLAYING) ? 1 : 0;
}

uint32_t RecPlay_RecordedChunks(void)
{
    return rp_chunk;
}

int RecPlay_RecordDone(void)
{
    return rp_record_done;
}

int RecPlay_PlayDone(void)
{
    return rp_play_done;
}

/* --- DMA callbacks (ISR context) ---------------------------------------------
 * Double-buffer semantics: when a callback fires, the stream has switched to
 * the OTHER memory (CT bit toggled), so the buffer tied to this callback is
 * complete and free to use:
 *   CT==1  -> currently using M1, so M0 (chunk0 / dummy_left) just finished
 *   CT==0  -> currently using M0, so M1 (chunk1 / dummy_right) just finished
 */

void RecPlay_DMA_Callback(DMA_HandleTypeDef *hdma)
{
    uint16_t *filled;

    if ((hdma != &hdma_rx) || (rp_state != RP_RECORDING)) { return; }

    filled = ((hdma->Instance->CR & DMA_SxCR_CT) != 0U) ? chunk0 : chunk1;

    if (rp_chunk < rp_total)
    {
        memcpy(&pcm_buffer[rp_chunk * CHUNK_SAMPLES], filled,
               CHUNK_SAMPLES * 2U);
        rp_chunk++;
    }
    if (rp_chunk >= rp_total)
    {
        rp_record_done = 1;              /* buffer full: 30 s reached */
        Engine_Stop();
    }
}

/* TX silence stream during recording: nothing to do (buffers stay zero). */
void RecPlay_DMA_Silence(DMA_HandleTypeDef *hdma)
{
    (void)hdma;
}

/* TX prefetch during playback: refill the chunk that just drained.
 * Every TC event means one buffer finished airing, so chunks air exactly in
 * the order they were loaded (pcm[0], pcm[1], ...); stop at aired == total. */
void RecPlay_DMA_Prefetch(DMA_HandleTypeDef *hdma)
{
    uint16_t *freed;

    if (rp_state != RP_PLAYING) { return; }

    /* CT==1 -> M0 (chunk0) drained; refilling it is safe. */
    freed = ((hdma->Instance->CR & DMA_SxCR_CT) != 0U) ? chunk0 : chunk1;

    rp_play_aired++;
    if (rp_play_loaded < rp_total)
    {
        memcpy(freed, &pcm_buffer[rp_play_loaded * CHUNK_SAMPLES],
               CHUNK_SAMPLES * 2U);
        rp_play_loaded++;
    }
    else if (rp_play_aired >= rp_total)
    {
        /* The final chunk just finished airing. */
        rp_play_done = 1;
        Engine_Stop();
    }
    else
    {
        memset(freed, 0, CHUNK_SAMPLES * 2U);   /* defensive silence */
    }
}

void RecPlay_DMA_Error(DMA_HandleTypeDef *hdma)
{
    (void)hdma;
    Engine_Stop();
}

/* Handle accessors for the DMA ISR shims in main.c. */
DMA_HandleTypeDef *RecPlay_TxHandle(void) { return &hdma_tx; }
DMA_HandleTypeDef *RecPlay_RxHandle(void) { return &hdma_rx; }
