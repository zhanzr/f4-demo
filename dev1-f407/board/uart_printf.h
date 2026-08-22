/**
  * @file    uart_printf.h
  * @brief   UART printf backend for the custom STM32F407VET6 board.
  *
  * printf() output is redirected to USART3 (PD8 = TX, PD9 = RX, AF7),
  * 115200 8-N-1, wired to the on-board RS232/RS485 transceivers.
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
