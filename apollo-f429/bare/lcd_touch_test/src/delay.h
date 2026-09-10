#ifndef __DELAY_H
#define __DELAY_H

#include "sys_compat.h"

/* Apollo-F429 / STM32F429IGT6 delay shim (SYSTEM/delay.h equivalent).
 * Delay functions are needed by the vendored touch/lcd code:
 *   delay_init(), delay_ms(), delay_us().
 * delay_ms() uses the HAL SysTick (HAL_GetTick), delay_us() is a calibrated
 * busy loop. */

void delay_init(u8 SYSCLK);
void delay_ms(u16 nms);
void delay_us(u32 nus);

#endif /* __DELAY_H */