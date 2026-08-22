/**
  * @file    board.h
  * @brief   Board support for the STM32F407VET6 "nano-f407" board.
  *
  * LED:  LED1-PB0, low-active (LOW = ON). This is the only user LED.
  * Console: USART1 PA9 (TX) / PA10 (RX), 115200 8-N-1, wired to the ST-Link VCP.
  */

#ifndef __BOARD_H__
#define __BOARD_H__

#include "stm32f4xx_hal.h"

/* --- LED (low-active, single LED on PB0) ----------------------------------- */
#define LED1_Pin        GPIO_PIN_0
#define LED_GPIO_Port   GPIOB

#define LED_ON()        HAL_GPIO_WritePin(LED_GPIO_Port, LED1_Pin, GPIO_PIN_RESET)
#define LED_OFF()       HAL_GPIO_WritePin(LED_GPIO_Port, LED1_Pin, GPIO_PIN_SET)
#define LED_TOGGLE()    HAL_GPIO_TogglePin(LED_GPIO_Port, LED1_Pin)

/* --- Init ------------------------------------------------------------------ */
void Board_Init(void);          /* clocks (168 MHz), LED GPIO, UART (USART1 PA9/PA10) */
void SystemClock_Config(void);  /* HSE 8 MHz -> PLL -> 168 MHz */
void Error_Handler(void);

#endif /* __BOARD_H__ */
