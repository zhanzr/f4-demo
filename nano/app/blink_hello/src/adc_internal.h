/**
  * @file    adc_internal.h
  * @brief   ADC1 internal-channel sampling for the STM32F407 "nano" board.
  *
  * The three internal channels of the master ADC1 peripheral:
  *   - ADC1_IN16  temperature sensor
  *   - ADC1_IN17  VREFINT (internal reference voltage, ~1.21 V)
  *   - ADC1_IN18  VBAT (battery/backup supply through the ADC)
  *
  * All three are only available on ADC1. The F407 is an F40x/F41x device, so
  * the temperature sensor is on IN16 (unlike F42x/F43x where it shares IN18
  * with VBAT); on this part the three internal channels can be converted in
  * one scan.
  */

#ifndef __ADC_INTERNAL_H__
#define __ADC_INTERNAL_H__

#include <stdint.h>

typedef struct {
    uint16_t raw_temp;    /* ADC1_IN16 code */
    uint16_t raw_vrefint; /* ADC1_IN17 code */
    uint16_t raw_vbat;    /* ADC1_IN18 code */
} ADC_InternalResult;

void ADC_Internal_Init(void);
void ADC_Internal_Sample(ADC_InternalResult *res);

#endif /* __ADC_INTERNAL_H__ */
