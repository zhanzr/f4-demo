#include <stdio.h>
#include "board.h"
#include "adc_internal.h"

/* VREFINT typical value (1.18..1.24 V, typ 1.21). */
#define VREFINT_TYPICAL_MV   1210U

/* Factory temperature-sensor calibration (12-bit, VDDA = 3.3 V):
 *   TS_CAL1 @ 0x1FFF7A2C  ->  30 C
 *   TS_CAL2 @ 0x1FFF7A2E  -> 110 C        */
#define TS_CAL1_ADDR  ((uint16_t *)0x1FFF7A2CU)
#define TS_CAL2_ADDR  ((uint16_t *)0x1FFF7A2EU)

/* Temperature from the raw temp-sensor code, using the factory calibration.
 * Measured code re-scaled to the 3.3 V calibration reference. */
static int TempC_FromCode(uint32_t raw_temp, uint32_t vdda_mv)
{
    uint32_t cal1 = *TS_CAL1_ADDR;
    uint32_t cal2 = *TS_CAL2_ADDR;

    if (cal1 == 0U || cal2 <= cal1)
    {
        return 0;   /* no valid calibration data */
    }
    uint32_t adc_scaled = (raw_temp * 3300UL) / vdda_mv;
    if (adc_scaled <= cal1)
    {
        return 30;
    }
    if (adc_scaled >= cal2)
    {
        return 110;
    }
    return 30 + (int)((adc_scaled - cal1) * 80UL / (cal2 - cal1));
}

static void SampleAndReport(void)
{
    ADC_InternalResult adc;
    ADC_Internal_Sample(&adc);

    /* Actual supply voltage from VREFINT. */
    uint32_t vdda_mv = 0;
    if (adc.raw_vrefint != 0U)
    {
        vdda_mv = (VREFINT_TYPICAL_MV * 4095UL) / adc.raw_vrefint;
    }

    int temp_c = TempC_FromCode(adc.raw_temp, vdda_mv);

    /* VBAT: on F42x/F43x the VBAT channel measures VBAT/3 internally. */
    uint32_t vbat_mv = 0;
    if (adc.raw_vbat != 0U)
    {
        vbat_mv = (vdda_mv * adc.raw_vbat) / 4095UL * 3UL;
    }

    printf("ADC: VREFINT=%hu code (%lu mV), temp=%hu code (%d C), VBAT=%hu code (%lu mV)\r\n",
           adc.raw_vrefint, (unsigned long)vdda_mv,
           adc.raw_temp, temp_c,
           adc.raw_vbat, (unsigned long)vbat_mv);
}

int main(void)
{
    HAL_Init();
    Board_Init();
    ADC_Internal_Init();

    printf("\r\n==== fire-f429 (STM32F429IGT6) blink_hello @ 180 MHz ====\r\n");
    printf("SYSCLK = %lu Hz (%lu MHz)\r\n",
           (unsigned long)SystemCoreClock,
           (unsigned long)(SystemCoreClock / 1000000UL));

    uint32_t phase = 0;
    uint32_t last_report = 0;

    while (1)
    {
        /* Blink the four LEDs (all low-active) one by one. */
        switch (phase % 4)
        {
        case 0: LED_R_ON();  LED_G_OFF(); LED_B_OFF(); LED_1_OFF(); break;
        case 1: LED_R_OFF(); LED_G_ON();  LED_B_OFF(); LED_1_OFF(); break;
        case 2: LED_R_OFF(); LED_G_OFF(); LED_B_ON();  LED_1_OFF(); break;
        default: LED_R_OFF(); LED_G_OFF(); LED_B_OFF(); LED_1_ON();  break;
        }
        phase++;
        HAL_Delay(250);

        /* Every 4 phases (~1 s): sample + report the internal channels. */
        if (phase % 4 == 0)
        {
            uint32_t now = HAL_GetTick();
            if (now - last_report >= 1000)
            {
                last_report = now;
                SampleAndReport();
            }
        }
    }

    return 0;
}