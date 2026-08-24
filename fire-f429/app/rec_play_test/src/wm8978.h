/*
 * WM8978 audio codec driver - fire-f429 board.
 * See wm8978.c for details.
 */
#ifndef __WM8978_H__
#define __WM8978_H__

#include "stm32f4xx_hal.h"

/* Input path selector bits (see WM8978_CfgAudioPath). */
enum
{
    IN_PATH_OFF  = 0x00,
    MIC_LEFT_ON  = 0x01,   /* LIP/LIN pins */
    MIC_RIGHT_ON = 0x02,   /* RIP/RIN pins */
    LINE_ON      = 0x04,   /* L2/R2 inputs */
    AUX_ON       = 0x08,   /* AUXL/AUXR inputs */
    DAC_ON       = 0x10,   /* I2S DAC (playback from MCU) */
    ADC_ON       = 0x20    /* ADC to I2S (recording to MCU) */
};

/* Output path selector bits. */
enum
{
    OUT_PATH_OFF = 0x00,
    EAR_LEFT_ON  = 0x01,   /* LOUT1 */
    EAR_RIGHT_ON = 0x02,   /* ROUT1 */
    SPK_ON       = 0x04,   /* LOUT2/ROUT2 */
    OUT3_4_ON    = 0x08
};

int  WM8978_Init(void);
int  WM8978_Reset(void);
void WM8978_CfgAudioPath(uint16_t in_path, uint16_t out_path);
void WM8978_CfgAudioIF(void);
void WM8978_SetMicGain(uint8_t gain);
void WM8978_SetOUT1Volume(uint8_t vol);
void WM8978_SetOUT2Volume(uint8_t vol);

#endif /* __WM8978_H__ */
