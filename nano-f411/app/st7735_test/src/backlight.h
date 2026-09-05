/*
  backlight.h - ST7735S backlight (BL = PB9) PWM driver.
  ~20% duty via TIM4_CH4 on PB9 (AF2). TIM4 is clocked from APB1; at the
  nano-f411 100 MHz clock tree (APB1 = 50 MHz, timer x2 = 100 MHz) a 1 kHz
  PWM is PSC=99, ARR=999, CCR4=200.
*/

#ifndef __BACKLIGHT_H
#define __BACKLIGHT_H

#include <stdint.h>

void Backlight_Init(void);
void Backlight_SetDuty(uint16_t percent);

#endif /* __BACKLIGHT_H */