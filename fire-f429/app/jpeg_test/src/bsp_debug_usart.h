/**
  * @file    ov5640_to_lcd_clone/src/bsp_debug_usart.h
  * @brief   STUB for the vendor's debug-USART header.
  *
  * The vendor's bsp_i2c.h includes it for printf()/debug macros. This clone
  * uses the repo's board UART (printf -> USART1 via uart_printf.c), so only
  * stdio.h is needed here.
  */
#ifndef FIRE_F429_CLONE_DEBUG_USART_STUB_H
#define FIRE_F429_CLONE_DEBUG_USART_STUB_H

#include <stdio.h>

#endif /* FIRE_F429_CLONE_DEBUG_USART_STUB_H */
