/**
  * @file    adc_internal.c
  * @brief   ADC1 internal-channel sampling (temperature / VREFINT / VBAT).
  *
  * ADC1 on APB2. At the default 168 MHz clock tree PCLK2 = 84 MHz and the ADC
  * clock is PCLK2/4 = 21 MHz. This blink_hello_48m build runs at 48 MHz, where
  * PCLK2 = 48 MHz and PCLK2/4 = 12 MHz (both well within the 36 MHz F407 ADC
  * maximum). A single scan converts the three internal channels in sequence
  * (12-bit, right-aligned).
  */

#include "adc_internal.h"
#include "board.h"
#include "stm32f4xx_hal.h"

static ADC_HandleTypeDef hadc1;

/* ------------------------------------------------------------------------ */
void ADC_Internal_Init(void)
{
    __HAL_RCC_ADC1_CLK_ENABLE();

    hadc1.Instance                    = ADC1;
    hadc1.Init.ClockPrescaler         = ADC_CLOCK_SYNC_PCLK_DIV4;  /* 84/4 = 21 MHz */
    hadc1.Init.Resolution             = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode           = ENABLE;      /* scan the 3 internal channels */
    hadc1.Init.ContinuousConvMode     = DISABLE;     /* one scan per HAL_ADC_Start */
    hadc1.Init.DiscontinuousConvMode  = DISABLE;
    hadc1.Init.ExternalTrigConvEdge   = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ExternalTrigConv       = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign              = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion        = 3;           /* temp, vrefint, vbat */
    hadc1.Init.DMAContinuousRequests  = DISABLE;
    hadc1.Init.EOCSelection           = ADC_EOC_SINGLE_CONV;
    if (HAL_ADC_Init(&hadc1) != HAL_OK)
    {
        Error_Handler();
    }

    /* Rank 1: temperature sensor (IN16). The F4 temp sensor needs a long
     * sample time (datasheet min ~10 us): 480 cycles @ 21 MHz = 22.9 us. */
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel      = ADC_CHANNEL_TEMPSENSOR;   /* = IN16 on F407 */
    sConfig.Rank         = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_480CYCLES;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }

    /* Rank 2: VREFINT (IN17). */
    sConfig.Channel      = ADC_CHANNEL_VREFINT;      /* = IN17 */
    sConfig.Rank         = 2;
    sConfig.SamplingTime = ADC_SAMPLETIME_480CYCLES;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }

    /* Rank 3: VBAT (IN18). */
    sConfig.Channel      = ADC_CHANNEL_VBAT;         /* = IN18 */
    sConfig.Rank         = 3;
    sConfig.SamplingTime = ADC_SAMPLETIME_480CYCLES;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }

    /* Enable the internal channels: temp sensor + VREFINT need TSVREFE, VBAT
     * needs VBATE (both in ADC1->CCR, set by HAL_ADC_ConfigChannel above). */
    __HAL_ADC_ENABLE(&hadc1);
}

/* ------------------------------------------------------------------------ */
void ADC_Internal_Sample(ADC_InternalResult *res)
{
    HAL_ADC_Start(&hadc1);   /* single scan of 3 conversions */
    HAL_ADC_PollForConversion(&hadc1, 10U);  res->raw_temp    = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10U);  res->raw_vrefint = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10U);  res->raw_vbat    = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
}