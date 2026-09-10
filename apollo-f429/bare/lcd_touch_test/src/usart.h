#ifndef __USART_H
#define __USART_H

/* usart.h shim for the vendored touch code. printf() already routes to the
 * board's UART through the newlib _write() backend (board/syscalls.c), so this
 * only needs to declare the init entry points the vendored drivers call. */

#include "sys_compat.h"

void uart_init(u32 bound);

#endif /* __USART_H */