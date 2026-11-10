/*
  backlight.c - LCD backlight (BL = PE9) PWM via TIM1_CH1.

  PE9 is TIM1_CH1 (AF1). TIM1 is an advanced timer on APB2; with the
  F413 100 MHz clock tree APB2 = 100 MHz and the timer clock is 100 MHz.
  PSC/ARR of 99/999 gives a 1 kHz PWM; CCR1 = 0..999 scales 0..100%.
  Advanced timer: MOE (BDTR) must be set or the outputs stay disconnected.

  Plain register setup (no HAL TIM module needed for this small demo).
*/

#include "backlight.h"
#include "lcd.h"

void Backlight_Init(void)
{
    GPIO_InitTypeDef g;

    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_TIM1_CLK_ENABLE();

    /* PE9 = TIM1_CH1, alternate function push-pull. */
    g.Pin   = LCD_BL_Pin;
    g.Mode  = GPIO_MODE_AF_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF1_TIM1;
    HAL_GPIO_Init(GPIOE, &g);

    /* 1 kHz PWM: timer clock 100 MHz / (PSC+1) / (ARR+1). */
    TIM1->CR1  = 0;
    TIM1->PSC  = 99;
    TIM1->ARR  = 999;
    TIM1->CCR1 = 150;                  /* 15% at init (main re-sets it) */
    TIM1->CCMR1 |= (TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2)  /* PWM mode 1 */
                 | TIM_CCMR1_OC1PE;    /* preload enabled */
    TIM1->CCER  |= TIM_CCER_CC1E;      /* CH1 output enable */
    TIM1->BDTR |= TIM_BDTR_MOE;        /* advanced timer: main output enable */
    TIM1->CR1   |= TIM_CR1_CEN;        /* start the counter */
}

void Backlight_SetDuty(uint16_t percent)
{
    if (percent > 100U)
    {
        percent = 100U;
    }
    TIM1->CCR1 = (uint32_t)percent * (TIM1->ARR + 1U) / 100U;
}

uint16_t Backlight_GetDuty(void)
{
    return (uint16_t)((TIM1->CCR1 * 100U) / (TIM1->ARR + 1U));
}
