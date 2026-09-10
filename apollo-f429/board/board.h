/**
  * @file    board.h
  * @brief   Board support for the apollo-f429 board (STM32F429IGT6).
  *
  * LEDs (both low-active, LOW = ON):
  *   LED1 - PB1, LED2 - PB0
  * Console: USART1 PA9 (TX) / PA10 (RX), 115200 8-N-1 -> USB-serial VCP.
  * DHT11: PB12.
  * AT24C02 EEPROM: I2C2 on PH4 (SCL) / PH5 (SDA), A0/A1/A2 to GND (addr 0x50).
  */

#ifndef __BOARD_H__
#define __BOARD_H__

#include "stm32f4xx_hal.h"

/* --- LEDs (low-active, LOW = ON) ------------------------------------------- */
#define LED1_Pin        GPIO_PIN_1
#define LED1_Port       GPIOB
#define LED2_Pin        GPIO_PIN_0
#define LED2_Port       GPIOB

#define LED1_ON()       HAL_GPIO_WritePin(LED1_Port, LED1_Pin, GPIO_PIN_RESET)
#define LED1_OFF()      HAL_GPIO_WritePin(LED1_Port, LED1_Pin, GPIO_PIN_SET)
#define LED1_TOGGLE()   HAL_GPIO_TogglePin(LED1_Port, LED1_Pin)

#define LED2_ON()       HAL_GPIO_WritePin(LED2_Port, LED2_Pin, GPIO_PIN_RESET)
#define LED2_OFF()      HAL_GPIO_WritePin(LED2_Port, LED2_Pin, GPIO_PIN_SET)
#define LED2_TOGGLE()   HAL_GPIO_TogglePin(LED2_Port, LED2_Pin)

/* Backward-compat aliases used by the migrated fire-f429 projects. */
#define LED_R_Pin       LED1_Pin
#define LED_R_Port      LED1_Port
#define LED_G_Pin       LED2_Pin
#define LED_G_Port      LED2_Port
#define LED_R_ON()      LED1_ON()
#define LED_R_OFF()     LED1_OFF()
#define LED_R_TOGGLE()  LED1_TOGGLE()
#define LED_G_ON()      LED2_ON()
#define LED_G_OFF()     LED2_OFF()
#define LED_G_TOGGLE()  LED2_TOGGLE()
#define LED_B_ON()      LED2_ON()
#define LED_B_OFF()     LED2_OFF()
#define LED_B_TOGGLE()  LED2_TOGGLE()
#define LED_1_Pin       LED1_Pin
#define LED_1_Port      LED1_Port
#define LED_1_ON()      LED1_ON()
#define LED_1_OFF()     LED1_OFF()
#define LED_1_TOGGLE()  LED1_TOGGLE()

/* --- DHT11 (PB12) ----------------------------------------------------------- */
#define DHT11_Pin       GPIO_PIN_12
#define DHT11_Port      GPIOB

/* --- AT24C02 (I2C2, PH4=SCL PH5=SDA, A0A1A2=GND) ---------------------------- */
#define EEPROM_I2C          I2C2
#define EEPROM_GPIO_Port    GPIOH
#define EEPROM_SCL_Pin      GPIO_PIN_4
#define EEPROM_SDA_Pin      GPIO_PIN_5
#define EEPROM_GPIO_AF      GPIO_AF4_I2C2
#define EEPROM_GPIO_CLK_EN  __HAL_RCC_GPIOH_CLK_ENABLE()
#define EEPROM_I2C_CLK_EN   __HAL_RCC_I2C2_CLK_ENABLE()

/* --- Init ------------------------------------------------------------------ */
void Board_Init(void);          /* clocks (180 MHz), LED GPIO, UART (USART1 PA9/PA10) */
void SystemClock_Config(void);  /* HSE 25 MHz -> PLL -> 180 MHz */
void Error_Handler(void);

#endif /* __BOARD_H__ */