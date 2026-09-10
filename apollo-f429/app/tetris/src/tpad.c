/* tpad.c - capacitive touch button (TIM2_CH1 / PA5) for the tetris game.
 * Ported from the vendored Apollo 实验10 (电容触摸按键实验) without the debug
 * "r:..." printf. The button drives a short capacitive charging edge on PA5;
 * TIM2 input-capture measures the discharge/charge time, which grows when a
 * finger is near the pad. TPAD_Init calibrates the idle value, TPAD_Touched()
 * reports the raw touch level (debounced by the game's button state machine).
 */
#include "tpad.h"
#include "delay.h"
#include "stm32f4xx_hal.h"

TIM_HandleTypeDef TIM2_Handler;

#define TPAD_ARR_MAX_VAL  0XFFFFFFFFUL   /* 32-bit counter                */

vu16 tpad_default_val = 0;

/* TPAD_Init: calibrate the idle touch value.
 * psc: TIM2 prescaler (ticks at HCLK/(8*... ) - vendored call is TPAD_Init(2)
 * on the 180 MHz clock). Returns 1 if the pad is absent / miswired. */
u8 TPAD_Init(u8 psc)
{
    u16 buf[10];
    u16 temp;
    u8  j, i;

    TIM2_CH1_Cap_Init(TPAD_ARR_MAX_VAL, psc - 1);

    for (i = 0; i < 10; i++)
    {
        buf[i] = TPAD_Get_Val();
        delay_ms(10);
    }
    for (i = 0; i < 9; i++)                /* simple bubble sort            */
    {
        for (j = i + 1; j < 10; j++)
        {
            if (buf[i] > buf[j])
            {
                temp = buf[i];
                buf[i] = buf[j];
                buf[j] = temp;
            }
        }
    }
    temp = 0;
    for (i = 2; i < 8; i++)                /* average the middle 6          */
    {
        temp += buf[i];
    }
    tpad_default_val = temp / 6;
    printf("tpad_default_val:%d\r\n", tpad_default_val);
    if (tpad_default_val > TPAD_ARR_MAX_VAL / 2)
    {
        return 1;                          /* pad not connected             */
    }
    return 0;
}

/* TPAD_Reset: charge the pad (PA5 output low -> input AF for capture). */
void TPAD_Reset(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();

    g.Pin   = GPIO_PIN_5;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_PULLDOWN;
    g.Speed = GPIO_SPEED_HIGH;
    HAL_GPIO_Init(GPIOA, &g);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
    delay_ms(5);

    __HAL_TIM_CLEAR_FLAG(&TIM2_Handler, TIM_FLAG_CC1 | TIM_FLAG_UPDATE);
    __HAL_TIM_SET_COUNTER(&TIM2_Handler, 0);

    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(GPIOA, &g);
}

/* measure the discharge time once; returns the raw counter value */
u16 TPAD_Get_Val(void)
{
    TPAD_Reset();
    while (__HAL_TIM_GET_FLAG(&TIM2_Handler, TIM_FLAG_CC1) == RESET)
    {
        if (__HAL_TIM_GET_COUNTER(&TIM2_Handler) > TPAD_ARR_MAX_VAL - 500)
        {
            return __HAL_TIM_GET_COUNTER(&TIM2_Handler);
        }
    }
    return HAL_TIM_ReadCapturedValue(&TIM2_Handler, TIM_CHANNEL_1);
}

/* TPAD_Get_MaxVal: take n samples, return the max that looks touched. */
u16 TPAD_Get_MaxVal(u8 n)
{
    u16 temp = 0;
    u16 res = 0;
    u8  lcntnum = n * 2 / 3;               /* require 2/3 of samples high   */
    u8  okcnt = 0;

    while (n--)
    {
        temp = TPAD_Get_Val();
        if (temp > (tpad_default_val * 5 / 4))
        {
            okcnt++;
        }
        if (temp > res)
        {
            res = temp;
        }
    }
    if (okcnt >= lcntnum)
    {
        return res;
    }
    return 0;
}

/* TPAD_Touched: raw touch level (no repeat guard) - 1 while the pad is
 * touched, debounced outside (the game feeds it through its button state
 * machine, so a sustained touch yields a single press edge). */
u8 TPAD_Touched(void)
{
    u16 rval = TPAD_Get_MaxVal(3);
    return (rval > (tpad_default_val * 4 / 3) &&
            rval < (10 * tpad_default_val)) ? 1 : 0;
}

/* TIM2 CH1 input capture init (32-bit up counter). */
void TIM2_CH1_Cap_Init(u32 arr, u16 psc)
{
    TIM_IC_InitTypeDef cfg = {0};

    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    TIM2_Handler.Instance               = TIM2;
    TIM2_Handler.Init.Prescaler         = psc;
    TIM2_Handler.Init.CounterMode       = TIM_COUNTERMODE_UP;
    TIM2_Handler.Init.Period            = arr;
    TIM2_Handler.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_IC_Init(&TIM2_Handler);

    cfg.ICPolarity      = TIM_ICPOLARITY_RISING;
    cfg.ICSelection     = TIM_ICSELECTION_DIRECTTI;
    cfg.ICPrescaler     = TIM_ICPSC_DIV1;
    cfg.ICFilter        = 0;
    HAL_TIM_IC_ConfigChannel(&TIM2_Handler, &cfg, TIM_CHANNEL_1);
    HAL_TIM_IC_Start(&TIM2_Handler, TIM_CHANNEL_1);
}