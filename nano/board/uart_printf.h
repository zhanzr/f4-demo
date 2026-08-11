/**
  * @file    uart_printf.h
  * @brief   UART printf backend for the STM32F407VET6 "nano" board.
  *
  * printf() output is redirected to USART1 (PA9 = TX, PA10 = RX, AF7),
  * 115200 8-N-1, wired to the ST-Link's virtual COM port (VCP).
  */

#ifndef __UART_PRINTF_H__
#define __UART_PRINTF_H__

void UART_Init(void);
int  UART_PutChar(int ch);

/*
 * printf() replacement for armclang builds (see cmake/printf_rename.h).
 * Defined in uart_printf.c; forwards to newlib vprintf() so output reaches
 * the UART through _write() -> UART_PutChar().
 */
int bench_printf(const char *fmt, ...);

#endif /* __UART_PRINTF_H__ */
