/*
  backlight.c - ST7735S backlight (BL = PB9) PWM via TIM4_CH4.
  PB9 is TIM4_CH4 (AF2). TIM4 is on APB1; with the nano-f411 100 MHz clock
  tree, APB1 = 50 MHz and the timer clock is x2 = 100 MHz. A PSC/ARR of
  99/999 gives a 1 kHz PWM; CCR4 = 0..999 scales 0..100%.

  Plain register setup (no HAL TIM module needed for this small demo).
*/

#include "backlight.h"
#include "lcd.h"

void Backlight_Init(void)
{
    GPIO_InitTypeDef g;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM4_CLK_ENABLE();

    /* PB9 = TIM4_CH4, alternate function push-pull. */
    g.Pin   = LCD_BL_Pin;
    g.Mode  = GPIO_MODE_AF_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF2_TIM4;
    HAL_GPIO_Init(GPIOB, &g);

    /* 1 kHz PWM: timer clock 100 MHz / (PSC+1) / (ARR+1). */
    TIM4->CR1  = 0;                    /* counter disabled while configuring */
    TIM4->PSC  = 99;
    TIM4->ARR  = 999;
    TIM4->CCR4 = 200;                  /* 20% at init */
    TIM4->CCMR2 |= (TIM_CCMR2_OC4M_1 | TIM_CCMR2_OC4M_2)  /* PWM mode 1 */
                 | TIM_CCMR2_OC4PE;    /* preload enabled */
    TIM4->CCER  |= TIM_CCER_CC4E;      /* CH4 output enable */
    TIM4->CR1   |= TIM_CR1_CEN;        /* start the counter */
}

void Backlight_SetDuty(uint16_t percent)
{
    if (percent > 100U)
    {
        percent = 100U;
    }
    TIM4->CCR4 = (uint32_t)percent * (TIM4->ARR + 1U) / 100U;
}