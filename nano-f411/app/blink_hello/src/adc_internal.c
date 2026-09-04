/**
  * @file    adc_internal.c
  * @brief   ADC1 internal-channel sampling (temperature / VREFINT / VBAT).
  *
  * ADC1 is on APB2 (50 MHz with the 100 MHz clock tree). The ADC clock is
  * PCLK2/4 = 12.5 MHz (within the 36 MHz max for F411). 480-cycle sample time
  * = 38.4 us (the F4 temp sensor needs a long sample, datasheet min ~10 us).
  *
  * F411 note: unlike the F407, the temperature sensor and VBAT share a single
  * ADC input (IN18), selected by the TSVREFE / VBATE bits in ADC1->CCR. Only
  * one of the two paths may be active at a time, and the temp sensor also
  * enables VREFINT - which is why VBAT gets its own conversion pass (rank 2,
  * VREFINT, returns no valid data in that pass and is discarded).
  *   pass A (TSVREFE):  IN18 = temperature sensor, IN17 = VREFINT
  *   pass B (VBATE):    IN18 = VBAT/4 (internal 1/4 divider), VREFINT off
  */

#include "adc_internal.h"
#include "board.h"
#include "stm32f4xx_hal.h"

static ADC_HandleTypeDef hadc1;

typedef enum { ADC_PASS_TEMP, ADC_PASS_VBAT } AdcPass;

static void SetPass(AdcPass pass)
{
    if (pass == ADC_PASS_TEMP)
    {
        ADC1_COMMON->CCR |=  ADC_CCR_TSVREFE;
        ADC1_COMMON->CCR &= ~ADC_CCR_VBATE;
    }
    else
    {
        ADC1_COMMON->CCR &= ~ADC_CCR_TSVREFE;
        ADC1_COMMON->CCR |=  ADC_CCR_VBATE;
    }
}

/* ------------------------------------------------------------------------ */
void ADC_Internal_Init(void)
{
    __HAL_RCC_ADC1_CLK_ENABLE();

    hadc1.Instance                    = ADC1;
    hadc1.Init.ClockPrescaler         = ADC_CLOCK_SYNC_PCLK_DIV4;  /* 50/4 = 12.5 MHz */
    hadc1.Init.Resolution             = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode           = ENABLE;      /* scan the 2 internal channels */
    hadc1.Init.ContinuousConvMode     = DISABLE;     /* one scan per HAL_ADC_Start */
    hadc1.Init.DiscontinuousConvMode  = DISABLE;
    hadc1.Init.ExternalTrigConvEdge   = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ExternalTrigConv       = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign              = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion        = 2;           /* IN18, VREFINT */
    hadc1.Init.DMAContinuousRequests  = DISABLE;
    hadc1.Init.EOCSelection           = ADC_EOC_SINGLE_CONV;
    if (HAL_ADC_Init(&hadc1) != HAL_OK)
    {
        Error_Handler();
    }

    /* Rank 1: IN18 - temperature sensor (pass A) or VBAT (pass B). On F411 the
     * TEMPSENSOR and VBAT channel identifiers are the same IN18 value. */
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel      = ADC_CHANNEL_TEMPSENSOR;   /* = IN18 on F411 */
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

    /* On F411 the TEMPSENSOR and VBAT identifiers are the same IN18 value, so
     * HAL_ADC_ConfigChannel cannot tell them apart and just leaves TSVREFE
     * set (its shared-channel special case clears VBATE). The two paths must
     * not be active together, so hand-drive the enable bits per pass
     * (see SetPass / ADC_Internal_Sample). */
    ADC1_COMMON->CCR &= ~(ADC_CCR_TSVREFE | ADC_CCR_VBATE);

    __HAL_ADC_ENABLE(&hadc1);
    SetPass(ADC_PASS_TEMP);
}

/* ------------------------------------------------------------------------ */
void ADC_Internal_Sample(ADC_InternalResult *res)
{
    /* Pass A: TSVREFE selected -> IN18 = temp sensor, IN17 = VREFINT. */
    SetPass(ADC_PASS_TEMP);
    HAL_Delay(1);              /* let the sensor/bandgap power up */
    HAL_ADC_Start(&hadc1);     /* single scan of 2 conversions */
    HAL_ADC_PollForConversion(&hadc1, 10U);  res->raw_temp    = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10U);  res->raw_vrefint = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    /* Pass B: VBATE selected -> IN18 reads VBAT/4; the VREFINT path is off, so
     * rank 2 returns no valid data - read it out and discard. */
    SetPass(ADC_PASS_VBAT);
    HAL_Delay(1);
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10U);  res->raw_vbat = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10U);  (void)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    /* Both internal paths off again (also avoids draining a battery through
     * the VBATE divider between samples); ADC_Internal_Sample re-enables the
     * needed path and lets it settle before converting. */
    ADC1_COMMON->CCR &= ~(ADC_CCR_TSVREFE | ADC_CCR_VBATE);
}