/**
  * @file    swv_printf.h
  * @brief   SWV/ITM (SWO) printf support.
  *
  * printf() is retargeted (via _write in syscalls.c) to ITM stimulus port 0,
  * which streams out the TRACESWO pin (PE2, AF0). Capture it with:
  *     probe-rs itm swo --chip STM32F407VE --protocol swd 600000 168000000 2000000
  * When no debugger is attached the writes are dropped (no blocking).
  */

#ifndef __SWV_PRINTF_H__
#define __SWV_PRINTF_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void SWV_Init(void);        /* enable DWT + ITM, stimulus port 0 */
int  SWV_PutChar(int ch);   /* non-blocking ITM port-0 write (0x00 drop) */

#ifdef __cplusplus
}
#endif

#endif /* __SWV_PRINTF_H__ */
