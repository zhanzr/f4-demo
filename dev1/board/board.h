/**
  * @file    board.h
  * @brief   Board support for the custom STM32F407VET6 board.
  *
  * LEDs:  LED1-PE13, LED2-PE14, LED3-PE15 (all LOW = ON).
  * Btns:  BTN1-PE10, BTN2-PE11, BTN3-PE12.
  */

#ifndef __BOARD_H__
#define __BOARD_H__

#include "stm32f4xx_hal.h"

/* --- LEDs (low-active) ----------------------------------------------------- */
#define LED1_Pin        GPIO_PIN_13
#define LED2_Pin        GPIO_PIN_14
#define LED3_Pin        GPIO_PIN_15
#define LED_GPIO_Port   GPIOE

#define LED1_ON()       HAL_GPIO_WritePin(LED_GPIO_Port, LED1_Pin, GPIO_PIN_RESET)
#define LED1_OFF()      HAL_GPIO_WritePin(LED_GPIO_Port, LED1_Pin, GPIO_PIN_SET)
#define LED1_TOGGLE()   HAL_GPIO_TogglePin(LED_GPIO_Port, LED1_Pin)

#define LED2_ON()       HAL_GPIO_WritePin(LED_GPIO_Port, LED2_Pin, GPIO_PIN_RESET)
#define LED2_OFF()      HAL_GPIO_WritePin(LED_GPIO_Port, LED2_Pin, GPIO_PIN_SET)
#define LED2_TOGGLE()   HAL_GPIO_TogglePin(LED_GPIO_Port, LED2_Pin)

#define LED3_ON()       HAL_GPIO_WritePin(LED_GPIO_Port, LED3_Pin, GPIO_PIN_RESET)
#define LED3_OFF()      HAL_GPIO_WritePin(LED_GPIO_Port, LED3_Pin, GPIO_PIN_SET)
#define LED3_TOGGLE()   HAL_GPIO_TogglePin(LED_GPIO_Port, LED3_Pin)

/* --- Buttons (PE10/PE11/PE12, active-low, no pull on board) ---------------- */
#define BTN1_Pin        GPIO_PIN_10
#define BTN2_Pin        GPIO_PIN_11
#define BTN3_Pin        GPIO_PIN_12
#define BTN_GPIO_Port   GPIOE

/* --- Init ------------------------------------------------------------------ */
void Board_Init(void);          /* clocks (168 MHz), LED GPIO, UART (USART3 PD8/PD9) */
void SystemClock_Config(void);  /* HSE 25 MHz -> PLL -> 168 MHz */
void Error_Handler(void);

#endif /* __BOARD_H__ */
