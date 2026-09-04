/**
  * @file    adc_internal.h
  * @brief   ADC1 internal-channel sampling for the STM32F411 "nano-f411" board.
  *
  * Internal channels of the master ADC1 peripheral on F411:
  *   - ADC1_IN18  temperature sensor (pass A) / VBAT (pass B)  - shared input
  *   - ADC1_IN17  VREFINT (internal reference voltage, ~1.21 V)
  *
  * Unlike the F407, the F411 temp sensor and VBAT share IN18, so they are
  * converted in two passes selected by TSVREFE / VBATE in ADC1->CCR (mutually
  * exclusive). VREFINT is only valid while TSVREFE is set.
  */

#ifndef __ADC_INTERNAL_H__
#define __ADC_INTERNAL_H__

#include <stdint.h>

typedef struct {
    uint16_t raw_temp;    /* ADC1_IN18 code (temp-sensor pass) */
    uint16_t raw_vrefint; /* ADC1_IN17 code */
    uint16_t raw_vbat;    /* ADC1_IN18 code (VBATE pass, VBAT/4) */
} ADC_InternalResult;

void ADC_Internal_Init(void);
void ADC_Internal_Sample(ADC_InternalResult *res);

#endif /* __ADC_INTERNAL_H__ */