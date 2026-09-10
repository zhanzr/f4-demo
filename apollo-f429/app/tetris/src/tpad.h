#ifndef __TPAD_H
#define __TPAD_H

#include "sys_compat.h"

/* Capacitive touch button on TIM2_CH1 (PA5) - vendored Apollo 实验10
 * (电容触摸按键实验), ported to this repo's HAL + delay shims. Used as the
 * game's start/pause/resume button: TPAD_Touched() reports the raw touch
 * level, which the game debounces into a single press edge. */

extern vu16 tpad_default_val;

void TPAD_Reset(void);
u16  TPAD_Get_Val(void);
u16  TPAD_Get_MaxVal(u8 n);
u8   TPAD_Init(u8 systick);
u8   TPAD_Touched(void);      /* raw level: 1 while the pad is touched      */
void TIM2_CH1_Cap_Init(u32 arr, u16 psc);

#endif /* __TPAD_H */