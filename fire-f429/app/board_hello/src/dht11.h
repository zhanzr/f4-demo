/*
 * DHT11 temperature/humidity sensor - single-wire protocol on PE2.
 *
 * Wiring (fire-f429): PE2 <-> DHT11 DATA (only one pin; the module has an
 * on-board pull-up, so the MCU pin is open-drain + internal pull-up).
 *
 * Protocol:
 *   host: pull DATA low >= 18 ms (start), release.
 *   sensor: 80 us low (response) + 80 us high.
 *   40 data bits, MSB first: humidity_high, humidity_low, temp_high,
 *   temp_low, checksum (= sum of previous 4 bytes, low 8 bits).
 *   Each bit: 50 us low, then high; ~26-28 us high = '0', ~70 us = '1'.
 *
 * Timing uses the DWT cycle counter (168/180 MHz) for microsecond accuracy.
 */
#ifndef __DHT11_H__
#define __DHT11_H__

#include <stdint.h>

typedef struct {
    uint8_t  rh_int;   /* humidity integer part   */
    uint8_t  rh_dec;   /* humidity decimal part   */
    uint8_t  t_int;    /* temperature integer part */
    uint8_t  t_dec;    /* temperature decimal part */
    uint8_t  checksum; /* raw 5th byte            */
    int      valid;    /* 1 = checksum OK         */
} DHT11_Result;

/* Configures PE2 as open-drain output. */
void DHT11_Init(void);

/* Performs one full read (~25 ms due to the start pulse). Returns 1 on
 * success (sensor present + checksum ok), 0 on failure. */
int DHT11_Read(DHT11_Result *res);

#endif /* __DHT11_H__ */
