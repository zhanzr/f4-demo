/**
  * @file    stm32f4xx_it.c
  * @brief   Interrupt Service Routines.
  *          All other vectors fall through to Default_Handler (startup file).
  */

#include "stm32f4xx_hal.h"

/**
  * @brief  System tick handler -> HAL tick counter.
  */
void SysTick_Handler(void)
{
    HAL_IncTick();
}

/**
  * @brief  Hard fault: LED2 fast blink, then spin.
  */
void HardFault_Handler(void)
{
    while (1)
    {
    }
}
