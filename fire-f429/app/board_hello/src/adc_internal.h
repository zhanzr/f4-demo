/**
  * @file    adc_internal.h
  * @brief   ADC1 internal-channel sampling for fire-f429 (STM32F429IGT6).
  *
  * On the STM32F42x/F43x the internal channels are:
  *   - ADC1_IN17  VREFINT
  *   - ADC1_IN18  temperature sensor, SHARED with VBAT (only one at a time)
  *
  * Because the temperature sensor and VBAT share IN18 they are converted
  * one after the other (single-channel), never in the same scan.
  */

#ifndef __ADC_INTERNAL_H__
#define __ADC_INTERNAL_H__

#include <stdint.h>

typedef struct {
    uint16_t raw_vrefint; /* ADC1_IN17 code (VREFINT)                    */
    uint16_t raw_temp;    /* ADC1_IN18 code (TSVREFE selected)           */
    uint16_t raw_vbat;    /* ADC1_IN18 code (VBATE selected)             */
    uint16_t raw_ldr;     /* ADC1_IN4 code (PA4, GL5516 + 10k to GND)    */
    uint32_t ldr_mv;      /* PA4 voltage in mV (VDD-scaled via VREFINT)  */
} ADC_InternalResult;

void ADC_Internal_Init(void);
void ADC_Internal_Sample(ADC_InternalResult *res);

#endif /* __ADC_INTERNAL_H__ */