#ifndef __SYS_COMPAT_H
#define __SYS_COMPAT_H

/* Compile-time compat layer for the vendored ALIENTEK Apollo driver code
 * (tft_lcd_test + touch/实验30). Mirrors the vendor's SYSTEM/sys.h + delay.h +
 * usart.h surface on top of this repo's HAL + board layer. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "stm32f4xx_hal.h"
#include "board.h"

/* ---- vendor lexer types (from SYSTEM/sys.h) ---- */
typedef int32_t  s32;  typedef int16_t s16;  typedef int8_t  s8;
typedef const int32_t sc32; typedef const int16_t sc16; typedef const int8_t sc8;
typedef __IO int32_t vs32; typedef __IO int16_t vs16; typedef __IO int8_t vs8;
typedef uint32_t u32; typedef uint16_t u16; typedef uint8_t u8;
typedef const uint32_t uc32; typedef const uint16_t uc16; typedef const uint8_t uc8;
typedef __IO uint32_t vu32; typedef __IO uint16_t vu16; typedef __IO uint8_t vu8;

/* ---- bit-band GPIO aliases (Pxout(n)/Pxin(n)) ---- */
#define BITBAND(addr, bitnum) (((addr) & 0xF0000000UL) + 0x2000000UL + (((addr) & 0xFFFFFUL) << 5) + ((bitnum) << 2))
#define MEM_ADDR(addr)   (*(volatile unsigned long *)(addr))
#define BIT_ADDR(addr, bitnum) MEM_ADDR(BITBAND(addr, bitnum))

#define GPIOB_ODR_Addr   (GPIOB_BASE + 20U)
#define GPIOD_ODR_Addr   (GPIOD_BASE + 20U)
#define GPIOG_ODR_Addr   (GPIOG_BASE + 20U)
#define GPIOH_ODR_Addr   (GPIOH_BASE + 20U)
#define GPIOI_ODR_Addr   (GPIOI_BASE + 20U)

#define GPIOB_IDR_Addr   (GPIOB_BASE + 16U)
#define GPIOD_IDR_Addr   (GPIOD_BASE + 16U)
#define GPIOG_IDR_Addr   (GPIOG_BASE + 16U)
#define GPIOH_IDR_Addr   (GPIOH_BASE + 16U)
#define GPIOI_IDR_Addr   (GPIOI_BASE + 16U)

#define PBout(n)  BIT_ADDR(GPIOB_ODR_Addr, n)
#define PDout(n)  BIT_ADDR(GPIOD_ODR_Addr, n)
#define PGout(n)  BIT_ADDR(GPIOG_ODR_Addr, n)
#define PHout(n)  BIT_ADDR(GPIOH_ODR_Addr, n)
#define PIout(n)  BIT_ADDR(GPIOI_ODR_Addr, n)

#define PBin(n)   BIT_ADDR(GPIOB_IDR_Addr, n)
#define PDin(n)   BIT_ADDR(GPIOD_IDR_Addr, n)
#define PGin(n)   BIT_ADDR(GPIOG_IDR_Addr, n)
#define PHin(n)   BIT_ADDR(GPIOH_IDR_Addr, n)
#define PIin(n)   BIT_ADDR(GPIOI_IDR_Addr, n)

/* ---- delay (SYSTEM/delay.h) ---- */
void delay_init(u8 SYSCLK);
void delay_ms(u16 nms);
void delay_us(u32 nus);

/* usart printf already arrives through the board's _write() backend. */

#endif /* __SYS_COMPAT_H */