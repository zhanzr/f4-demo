/*
  backlight.c - LCD backlight (BL_CTR = PA1) PWM driver.

  PA1 muxed to TIM2_CH2 (AF1). TIM2 is clocked from APB1; at the
  nano-f407 168 MHz clock tree (APB1 = 42 MHz, timer x2 = 84 MHz) a
  1 kHz PWM is PSC=83, ARR=999, CCR2 = 0..999 scales 0..100%.
  Defaults to 15% (the round panel is bright); main() re-sets it.
*/

#include "backlight.h"
#include "stm32f4xx_hal.h"

void Backlight_Init(void)
{
    GPIO_InitTypeDef g;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();

    g.Pin       = GPIO_PIN_1;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(GPIOA, &g);

    /* 1 kHz PWM: timer clock 84 MHz / (PSC+1) / (ARR+1). */
    TIM2->CR1  = 0;                    /* counter disabled while configuring */
    TIM2->PSC  = 83;
    TIM2->ARR  = 999;
    TIM2->CCR2 = 150;                  /* 15% at init (main re-sets it) */
    TIM2->CCMR1 |= (TIM_CCMR1_OC2M_1 | TIM_CCMR1_OC2M_2)  /* PWM mode 1 */
                 | TIM_CCMR1_OC2PE;    /* preload enabled */
    TIM2->CCER  |= TIM_CCER_CC2E;      /* CH2 output enable */
    TIM2->CR1   |= TIM_CR1_CEN;        /* start the counter */
}

void Backlight_SetDuty(uint16_t percent)
{
    if (percent > 100U)
    {
        percent = 100U;
    }
    TIM2->CCR2 = (uint32_t)percent * (TIM2->ARR + 1U) / 100U;
}

uint16_t Backlight_GetDuty(void)
{
    return (uint16_t)((TIM2->CCR2 * 100U) / (TIM2->ARR + 1U));
}
