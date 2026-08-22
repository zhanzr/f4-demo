/**
  * @file    adc_internal.c
  * @brief   ADC1 internal-channel sampling (VREFINT / temperature / VBAT).
  *
  * ADC1 on APB2 (90 MHz with the 180 MHz clock tree). PCLK2/4 = 22.5 MHz
  * (within the 36 MHz max). Long 480-cycle sampling (~21 us) for the internal
  * sensors. On F42x/F43x the temperature sensor and VBAT share ADC1_IN18, so
  * each is configured and converted individually (HAL_ADC_ConfigChannel sets
  * TSVREFE vs VBATE automatically).
  */

#include "adc_internal.h"
#include "board.h"
#include "stm32f4xx_hal.h"

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
    __HAL_RCC_ADC1_CLK_ENABLE();

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
void ADC_Internal_Sample(ADC_InternalResult *res)
{
    /* VREFINT (IN17). */
    ConfigChannel(ADC_CHANNEL_VREFINT);
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10U);
    res->raw_vrefint = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    /* Temperature sensor (IN18, TSVREFE auto-selected). */
    ConfigChannel(ADC_CHANNEL_TEMPSENSOR);
    __HAL_ADC_ENABLE(&hadc1);
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10U);
    res->raw_temp = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    /* VBAT (IN18, VBATE auto-selected, TSVREFE cleared). */
    ConfigChannel(ADC_CHANNEL_VBAT);
    __HAL_ADC_ENABLE(&hadc1);
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10U);
    res->raw_vbat = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
}