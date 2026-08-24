/*
 * rec_play_test - capacitive touch pad (PA5) driven record & playback test
 * for the fire-f429 board (WM8978 codec on full-duplex I2S2, PCM in RAM).
 *
 * State machine (a "press" = pad down then up, i.e. fires on release;
 * presses during recording/playing are ignored until the action completes):
 *
 *   WAIT_RECORD --press--> RECORDING --30 s done--> WAIT_PLAY --press--> PLAYING
 *        ^                                                                        |
 *        +---------------------------- play done --------------------------------+
 *
 * - RECORDING: 30 s of MIC audio into the SDRAM buffer, PD12 LED ON.
 *   Nothing is monitored on the outputs (silent record); the pad is ignored.
 * - PLAYING:   plays until the recorded samples run out, PD12 LED ON.
 *   The pad is ignored; only "samples ran out" returns to WAIT_RECORD.
 * - Every state change is printed on the UART console as "[state] A -> B".
 *
 * No SD card / no FatFs: the PCM stays in the 8 MB onboard SDRAM
 * (~5.3 MB for 30 s of 44.1 kHz 16-bit stereo).
 */
#include <stdio.h>
#include "board.h"
#include "capsense.h"
#include "rec_play.h"
#include "wm8978.h"

#define REC_MS   15000U   /* record duration                    */
#define POLL_MS  20U      /* main-loop poll period              */
#define STALL_MS 15000U   /* engine stuck watchdog (>> session) */

typedef enum
{
    ST_WAIT_RECORD = 0,   /* after power-on: wait for a press       */
    ST_RECORDING,         /* recording; presses ignored             */
    ST_WAIT_PLAY,         /* recording done: wait for a press       */
    ST_PLAYING,           /* playing; presses ignored               */
} App_State;

static const char *state_name(App_State s)
{
    switch (s)
    {
        case ST_WAIT_RECORD: return "WAIT_RECORD";
        case ST_RECORDING:   return "RECORDING";
        case ST_WAIT_PLAY:   return "WAIT_PLAY";
        case ST_PLAYING:     return "PLAYING";
        default:             return "?";
    }
}

/* Watchdog helper for RECORDING/PLAYING: if the engine makes no progress
 * (same chunk counter) for STALL_MS, force the session closed so the board
 * always returns to a waiting state instead of hanging with the LED on. */
static int engine_stalled(uint32_t now, uint32_t progress)
{
    static uint32_t last_progress = 0xFFFFFFFFU;
    static uint32_t stall_since   = 0;

    if (progress != last_progress)
    {
        last_progress = progress;
        stall_since   = now;
        return 0;
    }
    return (now - stall_since) >= STALL_MS;
}

/* Print a state change on the UART console. */
static void state_goto(App_State *p, App_State next)
{
    if (*p == next) { return; }
    printf("[state] %s -> %s\r\n", state_name(*p), state_name(next));
    *p = next;
}

int main(void)
{
    uint32_t   rec_chunks = 0;      /* length of the last recording (chunks) */
    App_State  state      = ST_WAIT_RECORD;
    int        prev_pressed = 0;    /* raw pad level last poll               */
    int        swallow     = 0;     /* eat a press held across a boundary    */
    int        press_event;

    HAL_Init();
    Board_Init();              /* 180 MHz, LEDs, USART1 console, SDRAM    */

    if (CapSense_Init() != 0)
    {
        printf("rec_play_test: no capsense pad, aborting\r\n");
        while (1) { }
    }
    if (WM8978_Init() != 1)
    {
        printf("rec_play_test: WM8978 NOT on the I2C bus (check wiring!)\r\n");
    }
    else
    {
        printf("wm8978: present\r\n");
    }
    RecPlay_Init();

    printf("\r\n==== fire-f429 rec_play_test ====\r\n");
    printf("44.1 kHz / 16-bit / stereo, %u s into RAM (SDRAM)\r\n",
           (unsigned)(REC_MS / 1000U));
    printf("[state] power-on -> %s\r\n", state_name(state));

    prev_pressed = 0;
    swallow      = 0;
    while (1)
    {
        int pressed = CapSense_Scan();

        /* A "press" is a completed down-then-up cycle: the event fires on
         * the release edge (a release can only follow a press-down). */
        press_event = (!pressed && prev_pressed) ? 1 : 0;

        /* During RECORDING / PLAYING the pad is ignored. If it is down when
         * the action ends, that in-progress press must not count: swallow
         * everything until the pad has been released once. */
        if ((state == ST_RECORDING) || (state == ST_PLAYING))
        {
            if (pressed) { swallow = 1; }
            press_event = 0;
        }
        else if (swallow)
        {
            if (!pressed) { swallow = 0; }   /* release seen: press consumed */
            press_event = 0;
        }

        /* Fire only the transition belonging to the current state. */
        switch (state)
        {
            case ST_WAIT_RECORD:
                if (press_event)
                {
                    state_goto(&state, ST_RECORDING);
                    printf("rec: start, %u ms (LED on)\r\n", (unsigned)REC_MS);
                    LED_1_ON();
                    RecPlay_StartRecord();
                }
                break;

            case ST_RECORDING:
                /* Presses during the 30 s recording are ignored. */
                if (RecPlay_RecordDone())
                {
                    rec_chunks = RecPlay_StopRecord();
                    state_goto(&state, ST_WAIT_PLAY);
                    LED_1_OFF();
                    printf("rec: done, %u chunks (%u ms)\r\n",
                           (unsigned)rec_chunks,
                           (unsigned)RP_CHUNKS_TO_MS(rec_chunks));
                    if (rec_chunks == 0U)
                    {
                        /* Degenerate (empty) recording: stay recordable. */
                        state_goto(&state, ST_WAIT_RECORD);
                    }
                }                else if (engine_stalled(HAL_GetTick(),
                                        RecPlay_RecordedChunks()))
                {
                    printf("rec: ENGINE STALLED (err=%08lx), forcing stop\r\n",
                           (unsigned long)RecPlay_LastError());
                    rec_chunks = RecPlay_StopRecord();
                    state_goto(&state,
                               (rec_chunks > 0U) ? ST_WAIT_PLAY
                                                  : ST_WAIT_RECORD);
                    LED_1_OFF();
                }                break;

            case ST_WAIT_PLAY:
                if (press_event)
                {
                    state_goto(&state, ST_PLAYING);
                    printf("play: start, %u ms (LED on)\r\n",
                           (unsigned)RP_CHUNKS_TO_MS(rec_chunks));
                    LED_1_ON();
                    RecPlay_StartPlay(rec_chunks);
                }
                break;

            case ST_PLAYING:
                /* Presses during playback are ignored. */
                if (RecPlay_PlayDone())
                {
                    (void)RecPlay_Reset();          /* engine + codec idle */
                    state_goto(&state, ST_WAIT_RECORD);
                    LED_1_OFF();
                    printf("play: done\r\n");
                }
                else if (engine_stalled(HAL_GetTick(),
                                        RecPlay_PlayAired()))
                {
                    printf("play: ENGINE STALLED (err=%08lx, aired=%lu), "
                           "forcing stop\r\n",
                           (unsigned long)RecPlay_LastError(),
                           (unsigned long)RecPlay_PlayAired());
                    (void)RecPlay_Reset();
                    state_goto(&state, ST_WAIT_RECORD);
                    LED_1_OFF();
                }
                break;

            default:
                break;
        }
        prev_pressed = pressed;

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
