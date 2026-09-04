/**
  * @file    board.h
  * @brief   Board support for the STM32F411CEU6 "nano-f411" board.
  *
  * LED:  LED1-PC13, low-active (LOW = ON). This is the only user LED.
  * Button: USER-PA0, active-low (pressed = GND, read high otherwise with no pull).
  * Console: USART1 PA9 (TX) / PA10 (RX), 115200 8-N-1, wired to the ST-Link VCP.
  */

#ifndef __BOARD_H__
#define __BOARD_H__

#include "stm32f4xx_hal.h"

/* --- LED (low-active, single LED on PC13) ---------------------------------- */
#define LED1_Pin        GPIO_PIN_13
#define LED_GPIO_Port   GPIOC

#define LED_ON()        HAL_GPIO_WritePin(LED_GPIO_Port, LED1_Pin, GPIO_PIN_RESET)
#define LED_OFF()       HAL_GPIO_WritePin(LED_GPIO_Port, LED1_Pin, GPIO_PIN_SET)
#define LED_TOGGLE()    HAL_GPIO_TogglePin(LED_GPIO_Port, LED1_Pin)

/* --- User button (PA0, pressed -> GND) ------------------------------------ */
#define BTN_Pin         GPIO_PIN_0
#define BTN_GPIO_Port   GPIOA

#define BTN_PRESSED()   (HAL_GPIO_ReadPin(BTN_GPIO_Port, BTN_Pin) == GPIO_PIN_RESET)

/* --- Init ------------------------------------------------------------------ */
void Board_Init(void);          /* clocks (100 MHz), LED GPIO, button, UART (USART1 PA9/PA10) */
void SystemClock_Config(void);  /* HSE 25 MHz -> PLL -> 100 MHz */
void Error_Handler(void);

#endif /* __BOARD_H__ */