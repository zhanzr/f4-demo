/*
 * Capacitive touch pad on PA5 (TIM2_CH1 input capture).
 * See capsense.c for details.
 */
#ifndef __CAPSENSE_H__
#define __CAPSENSE_H__

#include <stdint.h>

/* Initialise the capsense pad and calibrate the baseline. Returns 0 on
 * success, non-zero if the baseline is out of range (no pad / no charge
 * resistor). */
int CapSense_Init(void);

/* Debounced press state: returns 1 while pressed, 0 while released. */
int CapSense_Scan(void);

/* Latest raw charge-time measurement (counter ticks). */
uint16_t CapSense_GetRaw(void);

#endif /* __CAPSENSE_H__ */
