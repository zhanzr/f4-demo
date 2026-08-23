/*
 * Capacitive touch pad driver - TIM2_CH1 input capture on PA5.
 *
 * Ported from the Wildfire (野火) F429 "TIM-电容按键" example:
 *   D:\board_database\...\32-TIM—电容按键\User\TouchPad\bsp_touchpad.c
 *
 * Principle: the pad's capacitance charges through the on-board series
 * resistor toward 3.3 V; the GPIO Schmitt trigger trips at ~V_IH and that
 * rising edge is captured by TIM2_CH1. A finger adds ~10-30 pF -> longer
 * R*C charge time -> larger CCR1.
 *
 *   TPAD_Reset()  : drive PA5 low (pulldown) for 5 ms to fully discharge,
 *                   clear capture flags, CNT = 0, then PA5 back to AF input.
 *   TPAD_Get_Val(): read CCR1 (charge time in counter ticks).
 *   Init          : baseline = average of the middle 6 of 10 sorted samples.
 *   Scan          : max-of-3 samples; pressed if raw > baseline + GATE.
 *
 * TIM2 on APB1: 45 MHz * 2 = 90 MHz; PSC = 23 -> 3.75 MHz counter tick
 * (~266.7 ns), ARR = 0xFFFF (~17.5 ms full scale).
 */
#include "capsense.h"

#include "stm32f4xx_hal.h"
#include <stdio.h>

#define TPAD_TIMx                    TIM2
#define TPAD_TIM_CLK_ENABLE()        __HAL_RCC_TIM2_CLK_ENABLE()
#define TPAD_TIM_GPIO_CLK_ENABLE()   __HAL_RCC_GPIOA_CLK_ENABLE()

#define TPAD_TIM_CHANNEL             TIM_CHANNEL_1
#define TPAD_TIM_CH_PORT             GPIOA
#define TPAD_TIM_CH_PIN              GPIO_PIN_5
#define TPAD_TIM_AF                  GPIO_AF1_TIM2

#define TPAD_ARR_MAX_VAL             0xFFFFU
#define TPAD_GATE_VAL                100U      /* press if raw > base + GATE */
#define TPAD_SAMPLES                 3U        /* max-of-N per scan */

static TIM_HandleTypeDef g_tim;
static volatile uint16_t tpad_default_val = 0;

/* Drive PA5 low (pulldown) for 5 ms to discharge, re-arm as AF input. */
static void TPAD_Reset(void)
{
    GPIO_InitTypeDef gpio;

    /* PA5 -> push-pull output with pulldown, driven LOW. */
    gpio.Pin   = TPAD_TIM_CH_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_HIGH;
    gpio.Pull  = GPIO_PULLDOWN;
    HAL_GPIO_Init(TPAD_TIM_CH_PORT, &gpio);
    HAL_GPIO_WritePin(TPAD_TIM_CH_PORT, TPAD_TIM_CH_PIN, GPIO_PIN_RESET);
    HAL_Delay(5);                          /* ensure full discharge */

    __HAL_TIM_CLEAR_FLAG(&g_tim, TIM_FLAG_CC1);
    __HAL_TIM_CLEAR_FLAG(&g_tim, TIM_FLAG_UPDATE);
    __HAL_TIM_SET_COUNTER(&g_tim, 0);      /* CNT = 0 */

    /* PA5 -> AF1 (TIM2_CH1) input, no pull: pad charges via external R. */
    gpio.Pin       = TPAD_TIM_CH_PIN;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Alternate = TPAD_TIM_AF;
    gpio.Speed     = GPIO_SPEED_HIGH;
    gpio.Pull      = GPIO_NOPULL;
    HAL_GPIO_Init(TPAD_TIM_CH_PORT, &gpio);
}

/* One charge-time measurement (counter ticks until the rising edge). */
static uint16_t TPAD_Get_Val(void)
{
    TPAD_Reset();
    while (__HAL_TIM_GET_FLAG(&g_tim, TIM_FLAG_CC1) == RESET)
    {
        if (__HAL_TIM_GET_COUNTER(&g_tim) > (TPAD_ARR_MAX_VAL - 500U))
        {
            return (uint16_t)__HAL_TIM_GET_COUNTER(&g_tim);   /* no edge */
        }
    }
    return (uint16_t)HAL_TIM_ReadCapturedValue(&g_tim, TPAD_TIM_CHANNEL);
}

/* Keep the maximum of n consecutive measurements. */
static uint16_t TPAD_Get_MaxVal(uint8_t n)
{
    uint16_t temp = 0, res = 0;
    while (n-- != 0U)
    {
        temp = TPAD_Get_Val();
        if (temp > res) res = temp;
    }
    return res;
}

/* TIM2_CH1 input capture init: PSC 23, ARR 0xFFFF, rising edge, direct TI1. */
static void TIMx_CHx_Cap_Init(void)
{
    GPIO_InitTypeDef  gpio;
    TIM_IC_InitTypeDef ic;

    TPAD_TIM_GPIO_CLK_ENABLE();
    TPAD_TIM_CLK_ENABLE();

    gpio.Pin       = TPAD_TIM_CH_PIN;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Alternate = TPAD_TIM_AF;
    gpio.Speed     = GPIO_SPEED_HIGH;
    gpio.Pull      = GPIO_NOPULL;
    HAL_GPIO_Init(TPAD_TIM_CH_PORT, &gpio);

    g_tim.Instance               = TPAD_TIMx;
    g_tim.Init.Prescaler         = 24U - 1U;      /* 90 MHz / 24 = 3.75 MHz */
    g_tim.Init.CounterMode       = TIM_COUNTERMODE_UP;
    g_tim.Init.RepetitionCounter = 0;
    g_tim.Init.Period            = TPAD_ARR_MAX_VAL;
    g_tim.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_IC_Init(&g_tim);

    ic.ICPolarity  = TIM_INPUTCHANNELPOLARITY_RISING;
    ic.ICSelection = TIM_ICSELECTION_DIRECTTI;
    ic.ICPrescaler = TIM_ICPSC_DIV1;
    ic.ICFilter    = 0;
    HAL_TIM_IC_ConfigChannel(&g_tim, &ic, TPAD_TIM_CHANNEL);

    HAL_TIM_IC_Start(&g_tim, TPAD_TIM_CHANNEL);   /* free-running capture */
}

int CapSense_Init(void)
{
    uint16_t buf[10];
    uint32_t temp = 0;
    uint8_t  i, j;

    TIMx_CHx_Cap_Init();

    /* 10 samples, 10 ms apart; sort; average the middle 6. */
    for (i = 0; i < 10U; i++)
    {
        buf[i] = TPAD_Get_Val();
        HAL_Delay(10);
    }
    for (i = 0; i < 9U; i++)
    {
        for (j = i + 1U; j < 10U; j++)
        {
            if (buf[i] > buf[j])
            {
                temp       = buf[i];
                buf[i]     = buf[j];
                buf[j]     = (uint16_t)temp;
            }
        }
    }
    temp = 0;
    for (i = 2U; i < 8U; i++)
    {
        temp += buf[i];
    }
    tpad_default_val = (uint16_t)(temp / 6U);

    printf("capsense: baseline=%u\r\n", (unsigned)tpad_default_val);
    if (tpad_default_val > (TPAD_ARR_MAX_VAL / 2U))
    {
        printf("capsense: calibration FAILED (no pad / no charge resistor?)\r\n");
        return 1;
    }
    printf("capsense: ready\r\n");
    return 0;
}

/* Debounced press/release state: a change takes effect only after 2
 * consecutive opposing scans (debounce against noise). */
int CapSense_Scan(void)
{
    static int    state = 0;   /* 0 = released, 1 = pressed */
    static uint8_t cnt  = 0;
    uint16_t rval = TPAD_Get_MaxVal(TPAD_SAMPLES);
    int raw;

    if ((rval > (uint16_t)(tpad_default_val + TPAD_GATE_VAL)) &&
        (rval < (uint16_t)(10U * tpad_default_val)))
    {
        raw = 1;
    }
    else
    {
        raw = 0;
    }

    if (raw == state)
    {
        cnt = 0;              /* stable - no pending change */
        return state;
    }

    /* raw != state: count consecutive opposing readings before switching. */
    if (++cnt >= 2U)
    {
        cnt = 0;
        state = raw;
    }
    return state;
}

uint16_t CapSense_GetRaw(void)
{
    return TPAD_Get_MaxVal(TPAD_SAMPLES);
}
