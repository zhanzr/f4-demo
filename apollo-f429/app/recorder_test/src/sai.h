/*
 * sai.h - SAI1 audio driver (clean port of the vendored ALIENTEK Apollo 46
 * recorder example). SAI1 Block A = master TX (DMA2 Stream3), Block B = slave
 * RX (DMA2 Stream5), clock from PLLI2S. Pins: PE2(FS_A) PE3(SCK_A) PE4(SD_A)
 * PE6(MCLK_A) PE5(FS_B) AF6 SAI1.
 */
#ifndef __SAI_H
#define __SAI_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

extern SAI_HandleTypeDef SAI1A_Handler;
extern SAI_HandleTypeDef SAI1B_Handler;
extern DMA_HandleTypeDef SAI1_TXDMA_Handler;
extern DMA_HandleTypeDef SAI1_RXDMA_Handler;
extern void (*sai_tx_callback)(void);
extern void (*sai_rx_callback)(void);

void SAIA_Init(uint32_t mode, uint32_t cpol, uint32_t datalen);
void SAIB_Init(uint32_t mode, uint32_t cpol, uint32_t datalen);
void SAI1_Reset(void);
void SAIA_DMA_Enable(void);
void SAIB_DMA_Enable(void);
uint8_t SAIA_SampleRate_Set(uint32_t samplerate);
void SAIA_TX_DMA_Init(uint8_t *buf0, uint8_t *buf1, uint16_t num, uint8_t width);
void SAIA_RX_DMA_Init(uint8_t *buf0, uint8_t *buf1, uint16_t num, uint8_t width);
void SAI_Play_Start(void);
void SAI_Play_Stop(void);
void SAI_Rec_Start(void);
void SAI_Rec_Stop(void);
void SAI_DMA_ResetAll(void);

#endif /* __SAI_H */