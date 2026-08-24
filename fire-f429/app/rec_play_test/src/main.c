/*
 * rec_play_test - capacitive touch pad (PA5) driven record & playback test
 * for the fire-f429 board (WM8978 codec on full-duplex I2S2, PCM in RAM).
 *
 * Behavior:
 *   1. Press the capsense pad  -> record 30 s of MIC audio into the SDRAM
 *                                 buffer; the PD12 LED (LED_1) is ON while
 *                                 recording and goes OFF when it stops.
 *   2. Press the pad again     -> play the recording back; the LED is ON
 *                                 while playing and OFF afterwards.
 *
 * No SD card / no FatFs: the PCM stays in the 8 MB onboard SDRAM
 * (~5.3 MB for 30 s of 44.1 kHz 16-bit stereo).
 */
#include <stdio.h>
#include "board.h"
#include "capsense.h"
#include "rec_play.h"
#include "wm8978.h"

#define REC_MS   30000U   /* record duration                    */
#define POLL_MS  20U      /* main-loop poll period              */

typedef enum { ST_READY_RECORD = 0, ST_READY_PLAY } App_State;

int main(void)
{
    uint32_t   rec_chunks = 0;      /* length of the last recording (chunks) */
    App_State  state = ST_READY_RECORD;
    int        prev_pressed;

    HAL_Init();
    Board_Init();              /* 180 MHz, LEDs, USART1 console, SDRAM    */

    if (CapSense_Init() != 0)
    {
        printf("rec_play_test: no capsense pad, aborting\r\n");
        while (1) { }
    }
    if (WM8978_Init() != 1)
    {
        printf("rec_play_test: WM8978 init error\r\n");
    }
    RecPlay_Init();

    printf("\r\n==== fire-f429 rec_play_test ====\r\n");
    printf("44.1 kHz / 16-bit / stereo, %u s into RAM (SDRAM)\r\n",
           (unsigned)(REC_MS / 1000U));
    printf("press the capsense pad: record, press again: play\r\n");

    prev_pressed = 0;
    while (1)
    {
        int pressed = CapSense_Scan();

        /* Rising edge of the pad starts an action, but only when idle. */
        if (pressed && !prev_pressed)
        {
            if ((state == ST_READY_RECORD))
            {
                printf("pad: start recording %u ms (LED on)\r\n",
                       (unsigned)REC_MS);
                LED_1_ON();
                RecPlay_StartRecord();
            }
            else if (!RecPlay_IsPlaying())
            {
                printf("pad: start playback (%u ms, LED on)\r\n",
                       (unsigned)RP_CHUNKS_TO_MS(rec_chunks));
                LED_1_ON();
                RecPlay_StartPlay(rec_chunks);
            }
        }
        prev_pressed = pressed;

        /* Recording finished by itself (30 s elapsed / buffer full):
         * next press will play. */
        if (RecPlay_RecordDone())
        {
            rec_chunks = RecPlay_StopRecord();
            state      = (rec_chunks > 0U) ? ST_READY_PLAY : ST_READY_RECORD;
            LED_1_OFF();
            printf("rec: done, %u chunks (%u ms)\r\n",
                   (unsigned)rec_chunks, (unsigned)RP_CHUNKS_TO_MS(rec_chunks));
        }

        /* Playback finished by itself: next press records again. */
        if (RecPlay_PlayDone())
        {
            (void)RecPlay_StopRecord();   /* resets engine + codec to idle  */
            state = ST_READY_RECORD;
            LED_1_OFF();
            printf("play: done, press to record again\r\n");
        }

        HAL_Delay(POLL_MS);
    }
}

/* --- DMA ISRs ------------------------------------------------------------- */

void DMA1_Stream3_IRQHandler(void)   /* I2S2ext RX */
{
    HAL_DMA_IRQHandler(RecPlay_RxHandle());
}

void DMA1_Stream4_IRQHandler(void)   /* SPI2 TX */
{
    HAL_DMA_IRQHandler(RecPlay_TxHandle());
}
