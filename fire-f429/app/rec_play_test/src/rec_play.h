/*
 * Record/play engine - fire-f429 board (WM8978 codec on full-duplex I2S2).
 * See rec_play.c for details.
 */
#ifndef __REC_PLAY_H__
#define __REC_PLAY_H__

#include "stm32f4xx_hal.h"

/* Init PLLI2S, I2S2(+ext) GPIO/peripheral and both DMA streams. */
int  RecPlay_Init(void);

/* Record up to 30 s of stereo 16-bit PCM @ 44.1 kHz into the SDRAM buffer.
 * The engine stops itself when the buffer is full (see RecPlay_RecordDone). */
void RecPlay_StartRecord(void);

/* Stop an in-progress recording early; returns the chunks recorded so far. */
uint32_t RecPlay_StopRecord(void);

/* Play back the first `chunks` chunks of the recording. The engine stops
 * itself when done (see RecPlay_PlayDone). */
void RecPlay_StartPlay(uint32_t chunks);

int      RecPlay_IsPlaying(void);
uint32_t RecPlay_RecordedChunks(void);
int      RecPlay_RecordDone(void);
int      RecPlay_PlayDone(void);

/* DMA callbacks (wired to the stream handles; invoked from the DMA ISRs). */
void RecPlay_DMA_Callback(DMA_HandleTypeDef *hdma);   /* RX during record   */
void RecPlay_DMA_Silence(DMA_HandleTypeDef *hdma);    /* TX during record   */
void RecPlay_DMA_Prefetch(DMA_HandleTypeDef *hdma);   /* TX during play     */
void RecPlay_DMA_Error(DMA_HandleTypeDef *hdma);

/* Handle accessors for the DMA ISR shims in main.c. */
DMA_HandleTypeDef *RecPlay_TxHandle(void);
DMA_HandleTypeDef *RecPlay_RxHandle(void);

/* Convert chunks <-> milliseconds (20 ms per chunk). */
#define RP_CHUNK_MS   20U
#define RP_MS_TO_CHUNKS(ms)   (((ms) + RP_CHUNK_MS - 1U) / RP_CHUNK_MS)
#define RP_CHUNKS_TO_MS(n)    ((n) * RP_CHUNK_MS)

#endif /* __REC_PLAY_H__ */
