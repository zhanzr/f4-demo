/*
 * recorder_test - apollo-f429 stage-2 app: record 10 s from the WM8978 mic via
 * SAI1, then play it back on the speaker (also WM8978). No SD card / no WAV
 * wrapper: raw 16-bit stereo PCM is captured into SDRAM and streamed back.
 *
 *   recording  : WM8978 ADC on, SAI1 Block A master TX (clock) + Block B slave
 *                RX (DMA2 Stream5) -> SDRAM. LED0 ON.
 *   playing    : WM8978 DAC on, SAI1 Block A TX (DMA2 Stream3) from SDRAM.
 *                LED0 OFF.
 *
 * Ported from the vendored ALIENTEK Apollo 实验46 录音机实验 (wm8978 + sai),
 * simplified to an in-memory loop (SDRAM is the volatile memory, booted there
 * by tool/boot).
 */

#include <stdio.h>
#include <string.h>
#include "board.h"
#include "wm8978.h"
#include "sai.h"

#define REC_SAMPLERATE   44100U
#define REC_SECS         10U
#define REC_BYTES_SEC    (REC_SAMPLERATE * 2U * 2U)   /* 16-bit stereo */
#define REC_TOTAL        (((REC_BYTES_SEC * REC_SECS) + (DMA_BUF_SIZE - 1U)) \
                          & ~(DMA_BUF_SIZE - 1U))     /* 4096-aligned; >= 10 s */

#define DMA_BUF_SIZE     4096U

/* Audio PCM in SDRAM (the app itself lives in SDRAM; big array here is fine). */
static uint8_t  pcm_buf[REC_TOTAL];
static uint8_t  dma_rx0[DMA_BUF_SIZE];
static uint8_t  dma_rx1[DMA_BUF_SIZE];
static uint8_t  dma_tx0[DMA_BUF_SIZE];
static uint8_t  dma_tx1[DMA_BUF_SIZE];
static uint16_t silent_tx[2] = {0, 0};   /* zeros keep the bit clock running */

static volatile uint32_t rec_wpos;   /* next SDRAM write offset (record) */
static volatile uint32_t play_rpos;  /* next SDRAM read offset  (play)   */
static volatile uint8_t  mode;       /* 0 idle, 1 rec, 2 play             */

/* ------------------------------------------------------------------------ */
/* Record: SAI RX DMA callback - copy the just-finished half-buffer into
 * SDRAM. Bit 19 of the DMA stream CR says which buffer is currently active;
 * the OTHER one finished and must be consumed (mirrors the vendor logic). */
static void rec_cb(void)
{
    uint32_t src;

    if (DMA2_Stream5->CR & (1U << 19)) { src = (uint32_t)(uintptr_t)dma_rx0; }
    else                               { src = (uint32_t)(uintptr_t)dma_rx1; }

    if (rec_wpos + DMA_BUF_SIZE <= REC_TOTAL)
    {
        memcpy((uint8_t *)(uintptr_t)((uint32_t)(uintptr_t)pcm_buf + rec_wpos),
               (const void *)src, DMA_BUF_SIZE);
        rec_wpos += DMA_BUF_SIZE;
    }
}

/* Play: SAI TX DMA callback - refill the other half-buffer from SDRAM,
 * zero once the recorded data is exhausted. */
static void play_cb(void)
{
    uint8_t *dst;
    uint32_t n;

    if (DMA2_Stream3->CR & (1U << 19)) { dst = dma_tx0; }
    else                               { dst = dma_tx1; }

    n = REC_TOTAL - play_rpos;
    if (n > DMA_BUF_SIZE) { n = DMA_BUF_SIZE; }
    memcpy(dst, (const void *)(uintptr_t)((uint32_t)(uintptr_t)pcm_buf + play_rpos), n);
    play_rpos += n;
    if (n < DMA_BUF_SIZE)               /* tail: silence */
    {
        memset(dst + n, 0, DMA_BUF_SIZE - n);
    }
}

/* ------------------------------------------------------------------------ */
static void rec_enter_record(void)
{
    rec_wpos = 0;

    /* Clear any stale SAI/DMA state left by playback so both blocks re-arm
     * cleanly on every cycle (SAI1_Reset + DMA State back to READY). */
    SAI1_Reset();
    SAI_DMA_ResetAll();

    WM8978_ADDA_Cfg(0, 1);          /* ADC on, DAC off */
    WM8978_Input_Cfg(1, 1, 0);      /* MIC + LINE IN */
    WM8978_Output_Cfg(0, 1);        /* bypass */
    WM8978_MIC_Gain(46);
    WM8978_SPKvol_Set(0);
    WM8978_I2S_Cfg(2, 0);           /* standard I2S, 16-bit */

    SAIA_Init(SAI_MODEMASTER_TX, SAI_CLOCKSTROBING_RISINGEDGE, SAI_DATASIZE_16);
    SAIB_Init(SAI_MODESLAVE_RX, SAI_CLOCKSTROBING_RISINGEDGE, SAI_DATASIZE_16);
    SAIA_SampleRate_Set(REC_SAMPLERATE);
    SAIA_TX_DMA_Init((uint8_t *)&silent_tx[0], (uint8_t *)&silent_tx[1], 1, 1);
    __HAL_DMA_DISABLE_IT(&SAI1_TXDMA_Handler, DMA_IT_TC);
    SAIA_RX_DMA_Init(dma_rx0, dma_rx1, DMA_BUF_SIZE / 2U, 1);
    sai_rx_callback = rec_cb;

    SAI_Play_Start();
    SAI_Rec_Start();
    mode = 1;
}

static void rec_enter_play(void)
{
    play_rpos = 0;

    SAI_Play_Stop();
    SAI_Rec_Stop();
    SAI1_Reset();
    SAI_DMA_ResetAll();

    WM8978_ADDA_Cfg(1, 0);          /* DAC on, ADC off */
    WM8978_Input_Cfg(0, 0, 0);      /* inputs off */
    WM8978_Output_Cfg(1, 0);        /* DAC output */
    WM8978_MIC_Gain(0);
    WM8978_SPKvol_Set(50);          /* speaker volume */
    WM8978_I2S_Cfg(2, 0);

    SAIA_Init(SAI_MODEMASTER_TX, SAI_CLOCKSTROBING_RISINGEDGE, SAI_DATASIZE_16);
    SAIA_SampleRate_Set(REC_SAMPLERATE);
    SAIA_TX_DMA_Init(dma_tx0, dma_tx1, DMA_BUF_SIZE / 2U, 1);
    sai_tx_callback = play_cb;

    play_cb();                      /* prefill both half-buffers */
    play_cb();
    SAI_Play_Start();
    mode = 2;
}

/* ------------------------------------------------------------------------ */
int main(void)
{
    uint32_t last_blink = 0;

    HAL_Init();
    Board_Init();

    printf("\r\n==== apollo-f429 recorder_test @ %lu MHz ====\r\n",
           (unsigned long)(SystemCoreClock / 1000000UL));
    printf("WM8978 + SAI1, %u Hz 16-bit stereo, %u s in SDRAM\r\n",
           (unsigned)REC_SAMPLERATE, (unsigned)REC_SECS);

    if (WM8978_Init())
    {
        printf("WM8978 init FAILED\r\n");
    }
    else
    {
        printf("WM8978 init OK\r\n");
    }
    WM8978_HPvol_Set(40, 40);
    WM8978_SPKvol_Set(50);

    while (1)
    {
        /* --- record 10 s --- */
        printf("recording %u s (LED0 ON) ...\r\n", (unsigned)REC_SECS);
        LED0_ON();
        rec_enter_record();

        while ((mode == 1) && (rec_wpos < REC_TOTAL))
        {
            if (HAL_GetTick() - last_blink >= 250)
            {
                last_blink = HAL_GetTick();
                LED1_TOGGLE();
            }
        }
        SAI_Rec_Stop();
        SAI_Play_Stop();
        printf("recorded %lu bytes\r\n", (unsigned long)rec_wpos);

        /* --- play back --- */
        printf("playing (LED0 OFF) ...\r\n");
        LED0_OFF();
        rec_enter_play();

        while ((mode == 2) && (play_rpos < REC_TOTAL))
        {
            if (HAL_GetTick() - last_blink >= 500)
            {
                last_blink = HAL_GetTick();
                LED1_TOGGLE();
            }
        }
        SAI_Play_Stop();
        printf("playback done\r\n");
    }

    return 0;
}