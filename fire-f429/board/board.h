/**
  * @file    board.h
  * @brief   Board support for the fire-f429 board (STM32F429IGT6).
  *
  * LEDs (all low-active, LOW = ON):
  *   LED_R - PH10, LED_G - PH11, LED_B - PH12, LED_1 - PD12
  * Console: USART1 PA9 (TX) / PA10 (RX), 115200 8-N-1 -> USB-serial VCP.
  */

#ifndef __BOARD_H__
#define __BOARD_H__

#include "stm32f4xx_hal.h"

/* --- LEDs (low-active) ----------------------------------------------------- */
#define LED_R_Pin       GPIO_PIN_10
#define LED_R_Port      GPIOH
#define LED_G_Pin       GPIO_PIN_11
#define LED_G_Port      GPIOH
#define LED_B_Pin       GPIO_PIN_12
#define LED_B_Port      GPIOH
#define LED_1_Pin       GPIO_PIN_12
#define LED_1_Port      GPIOD

#define LED_R_ON()      HAL_GPIO_WritePin(LED_R_Port, LED_R_Pin, GPIO_PIN_RESET)
#define LED_R_OFF()     HAL_GPIO_WritePin(LED_R_Port, LED_R_Pin, GPIO_PIN_SET)
#define LED_R_TOGGLE()  HAL_GPIO_TogglePin(LED_R_Port, LED_R_Pin)

#define LED_G_ON()      HAL_GPIO_WritePin(LED_G_Port, LED_G_Pin, GPIO_PIN_RESET)
#define LED_G_OFF()     HAL_GPIO_WritePin(LED_G_Port, LED_G_Pin, GPIO_PIN_SET)
#define LED_G_TOGGLE()  HAL_GPIO_TogglePin(LED_G_Port, LED_G_Pin)

#define LED_B_ON()      HAL_GPIO_WritePin(LED_B_Port, LED_B_Pin, GPIO_PIN_RESET)
#define LED_B_OFF()     HAL_GPIO_WritePin(LED_B_Port, LED_B_Pin, GPIO_PIN_SET)
#define LED_B_TOGGLE()  HAL_GPIO_TogglePin(LED_B_Port, LED_B_Pin)

#define LED_1_ON()      HAL_GPIO_WritePin(LED_1_Port, LED_1_Pin, GPIO_PIN_RESET)
#define LED_1_OFF()     HAL_GPIO_WritePin(LED_1_Port, LED_1_Pin, GPIO_PIN_SET)
#define LED_1_TOGGLE()  HAL_GPIO_TogglePin(LED_1_Port, LED_1_Pin)

/* --- Init ------------------------------------------------------------------ */
void Board_Init(void);          /* clocks (180 MHz), LED GPIO, UART (USART1 PA9/PA10) */
void SystemClock_Config(void);  /* HSE 25 MHz -> PLL -> 180 MHz */
void Error_Handler(void);

#endif /* __BOARD_H__ */