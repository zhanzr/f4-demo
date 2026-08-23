/**
  * @file    adc_internal.c
  * @brief   ADC1 sampling: internal channels (VREFINT / temperature / VBAT)
  *          plus the external PA4 channel (GL5516 light-dependent resistor).
  *
  * ADC1 on APB2 (90 MHz with the 180 MHz clock tree). PCLK2/4 = 22.5 MHz
  * (within the 36 MHz max). Long 480-cycle sampling (~21 us) for the internal
  * sensors. On F42x/F43x the temperature sensor and VBAT share ADC1_IN18, so
  * each is configured and converted individually (HAL_ADC_ConfigChannel sets
  * TSVREFE vs VBATE automatically).
  *
  * PA4 = ADC1_IN4: [VDD] <=> GL5516 (light-strength resistor) <=> PA4 <=>
  * 10 kOhm <=> GND. Raw code + mV are reported (VDDA derived from VREFINT).
  */

#include "adc_internal.h"
#include "board.h"
#include "stm32f4xx_hal.h"

/* VREFINT typical value (1.18..1.24 V, typ 1.21). */
#define VREFINT_TYPICAL_MV   1210U

static ADC_HandleTypeDef hadc1;

/* ------------------------------------------------------------------------ */
static void ConfigChannel(uint32_t channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel      = channel;
    sConfig.Rank         = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_480CYCLES;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }
}

/* ------------------------------------------------------------------------ */
void ADC_Internal_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA4 = ADC1_IN4 analog input (GL5516 voltage divider). */
    gpio.Pin  = GPIO_PIN_4;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    hadc1.Instance                    = ADC1;
    hadc1.Init.ClockPrescaler         = ADC_CLOCK_SYNC_PCLK_DIV4;  /* 90/4 = 22.5 MHz */
    hadc1.Init.Resolution             = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode           = DISABLE;   /* one channel per conversion */
    hadc1.Init.ContinuousConvMode     = DISABLE;
    hadc1.Init.DiscontinuousConvMode  = DISABLE;
    hadc1.Init.ExternalTrigConvEdge   = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ExternalTrigConv       = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign              = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion        = 1;
    hadc1.Init.DMAContinuousRequests  = DISABLE;
    hadc1.Init.EOCSelection           = ADC_EOC_SINGLE_CONV;
    if (HAL_ADC_Init(&hadc1) != HAL_OK)
    {
        Error_Handler();
    }

    __HAL_ADC_ENABLE(&hadc1);
}

/* ------------------------------------------------------------------------ */
static uint16_t SampleChannel(uint32_t channel)
{
    uint16_t raw;

    ConfigChannel(channel);
    __HAL_ADC_ENABLE(&hadc1);
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10U);
    raw = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return raw;
}

/* ------------------------------------------------------------------------ */
void ADC_Internal_Sample(ADC_InternalResult *res)
{
    /* VREFINT (IN17). */
    res->raw_vrefint = SampleChannel(ADC_CHANNEL_VREFINT);

    /* Temperature sensor (IN18, TSVREFE auto-selected). */
    res->raw_temp = SampleChannel(ADC_CHANNEL_TEMPSENSOR);

    /* VBAT (IN18, VBATE auto-selected, TSVREFE cleared). */
    res->raw_vbat = SampleChannel(ADC_CHANNEL_VBAT);

    /* External PA4 / IN4 (GL5516 light-dependent resistor). */
    res->raw_ldr = SampleChannel(ADC_CHANNEL_4);

    /* Actual supply voltage from VREFINT, then PA4 in mV. */
    uint32_t vdda_mv = (VREFINT_TYPICAL_MV * 4095UL) / res->raw_vrefint;
    res->ldr_mv = (res->raw_ldr * vdda_mv) / 4095UL;
}