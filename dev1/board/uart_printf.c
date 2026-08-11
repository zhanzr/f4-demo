/**
  * @file    uart_printf.c
  * @brief   USART3 printf implementation for the custom STM32F407VET6 board.
  *
  * USART3 is on APB1 (42 MHz with the 168 MHz clock tree) and is wired to the
  * on-board RS232/RS485 transceivers via PD8 (TX) and PD9 (RX), AF7.
  * Output is 115200 8-N-1, blocking (polled) so nothing is dropped.
  */

#include "uart_printf.h"
#include "stm32f4xx_hal.h"

#include <stdio.h>
#include <stdarg.h>

static UART_HandleTypeDef huart3;

/* ------------------------------------------------------------------------ */
/* printf() replacement for armclang builds (see cmake/printf_rename.h).
 * armclang would otherwise turn printf into ARMCLIB's __2printf ABI, which
 * cannot be linked against newlib. vprintf() is not specialized by armclang,
 * so this thin wrapper keeps the standard printf() working over the UART. */
int bench_printf(const char *fmt, ...)
{
    va_list args;
    int r;

    va_start(args, fmt);
    r = vprintf(fmt, args);
    va_end(args);
    return r;
}

/* ------------------------------------------------------------------------ */
void UART_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* PD8 = USART3_TX, PD9 = USART3_RX (AF7). */
    GPIO_InitStruct.Pin       = GPIO_PIN_8 | GPIO_PIN_9;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_PULLUP;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    huart3.Instance          = USART3;
    huart3.Init.BaudRate     = 115200U;
    huart3.Init.WordLength   = UART_WORDLENGTH_8B;
    huart3.Init.StopBits     = UART_STOPBITS_1;
    huart3.Init.Parity       = UART_PARITY_NONE;
    huart3.Init.Mode         = UART_MODE_TX_RX;
    huart3.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart3.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart3);
}

/* ------------------------------------------------------------------------ */
int UART_PutChar(int ch)
{
    uint8_t c = (uint8_t)ch;
    HAL_UART_Transmit(&huart3, &c, 1U, 1000U);
    return ch;
}
