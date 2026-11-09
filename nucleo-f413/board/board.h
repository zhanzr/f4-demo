/**
  * @file    board.h
  * @brief   Board support for the Nucleo-F413ZH board (STM32F413ZHT6).
  *
  * LEDs:  LD1-PB0 (green), LD2-PB7 (blue), LD3-PB14 (red) - all high-active
  *        (pin SET = LED on).
  * Button: USER B1-PC13, active-high (pressed = 3V3, read low otherwise with
  *         the pull-down the board layer installs).
  * Console: USART3 PD8 (TX) / PD9 (RX), 115200 8-N-1 - the ST-LINK VCP.
  */

#ifndef __BOARD_H__
#define __BOARD_H__

#include "stm32f4xx_hal.h"

/* --- LEDs (high-active: pin SET = ON) -------------------------------------- */
#define LED1_Pin        GPIO_PIN_0      /* LD1 green  */
#define LED2_Pin        GPIO_PIN_7      /* LD2 blue   */
#define LED3_Pin        GPIO_PIN_14     /* LD3 red    */
#define LED1_GPIO_Port  GPIOB
#define LED2_GPIO_Port  GPIOB
#define LED3_GPIO_Port  GPIOB

#define LED1_ON()       HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET)
#define LED1_OFF()      HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET)
#define LED1_TOGGLE()   HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin)

#define LED2_ON()       HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_SET)
#define LED2_OFF()      HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_RESET)
#define LED2_TOGGLE()   HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin)

#define LED3_ON()       HAL_GPIO_WritePin(LED3_GPIO_Port, LED3_Pin, GPIO_PIN_SET)
#define LED3_OFF()      HAL_GPIO_WritePin(LED3_GPIO_Port, LED3_Pin, GPIO_PIN_RESET)
#define LED3_TOGGLE()   HAL_GPIO_TogglePin(LED3_GPIO_Port, LED3_Pin)

/* Generic alias for single-LED code / Error_Handler: LD1. */
#define LED_ON()        LED1_ON()
#define LED_OFF()       LED1_OFF()
#define LED_TOGGLE()    LED1_TOGGLE()

/* --- User button (B1 = PC13, pressed -> 3V3 = high) ------------------------ */
#define BTN_Pin         GPIO_PIN_13
#define BTN_GPIO_Port   GPIOC

#define BTN_PRESSED()   (HAL_GPIO_ReadPin(BTN_GPIO_Port, BTN_Pin) == GPIO_PIN_SET)

/* --- Init ------------------------------------------------------------------ */
void Board_Init(void);          /* clocks (100 MHz HSI PLL, F413 max), LEDs, button, UART (USART3 PD8/PD9) */
void SystemClock_Config(void);  /* HSI 16 MHz -> PLL -> 100 MHz */
void Error_Handler(void);

#endif /* __BOARD_H__ */
