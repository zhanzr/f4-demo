#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "stm32f4xx_hal.h"
#include "utils.h"

#define BUF_SIZE 512

void TICK_Init(void)
{
    /* HAL_Init() already configured the 1 ms SysTick. Nothing to do. */
}

void uart_printf(const char *fmt, ...)
{
    char buf[BUF_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    printf("%s", buf);
}
