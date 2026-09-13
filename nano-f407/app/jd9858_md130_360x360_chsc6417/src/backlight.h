/*
  backlight.h - LCD backlight (BL_CTR = PA1) PWM driver via TIM2_CH2.

  1 kHz PWM, duty 0..100% (default 15%).
*/

#ifndef __BACKLIGHT_H
#define __BACKLIGHT_H

#include <stdint.h>

void Backlight_Init(void);
void Backlight_SetDuty(uint16_t percent);
uint16_t Backlight_GetDuty(void);      /* current duty in percent */

#endif /* __BACKLIGHT_H */
