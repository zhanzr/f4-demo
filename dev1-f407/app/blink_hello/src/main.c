#include <stdio.h>
#include "board.h"
#include "adc_internal.h"

/* VREFINT typical value from the STM32F407 datasheet (1.18..1.24 V, typ 1.21). */
#define VREFINT_TYPICAL_MV   1210U

/* Factory-calibrated temperature-sensor ADC values (12-bit, VDDA = 3.3 V),
 * stored in system memory by ST:
 *   TS_CAL1 = raw ADC code at 30 C  (0x1FFF7A2C)
 *   TS_CAL2 = raw ADC code at 110 C (0x1FFF7A2E)  */
#define TS_CAL1_ADDR  ((uint16_t *)0x1FFF7A2CU)
#define TS_CAL2_ADDR  ((uint16_t *)0x1FFF7A2EU)

/* Temperature from the raw temp-sensor code, using the factory calibration.
 * The measured code is first re-scaled to the 3.3 V reference the calibration
 * was done at, then linearly interpolated between the two calibration points:
 *   T = 30 + (adc_scaled - TS_CAL1) * (110 - 30) / (TS_CAL2 - TS_CAL1)  */
static int TempC_FromCode(uint32_t raw_temp, uint32_t vdda_mv)
{
    uint32_t cal1 = *TS_CAL1_ADDR;
    uint32_t cal2 = *TS_CAL2_ADDR;

    if (cal1 == 0U || cal2 <= cal1)
    {
        return 0;   /* no valid calibration data */
    }
    /* Re-scale the measured code to the 3.3 V calibration reference. */
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

int main(void)
{
    HAL_Init();
    Board_Init();
    ADC_Internal_Init();

    printf("\r\n==== dev1-f407 (STM32F407VET6) blink_hello @ 168 MHz ====\r\n");
    printf("SYSCLK = %lu Hz (%lu MHz)\r\n",
           (unsigned long)SystemCoreClock,
           (unsigned long)(SystemCoreClock / 1000000UL));

    uint32_t phase = 0;
    uint32_t last_report = 0;

    while (1)
    {
        /* Basic operation: blink LED1/2/3 (PE13/14/15, low-active). */
        switch (phase % 4)
        {
        case 0:
            LED1_ON();
            LED2_OFF();
            LED3_OFF();
            break;
        case 1:
            LED1_OFF();
            LED2_ON();
            LED3_OFF();
            break;
        case 2:
            LED1_OFF();
            LED2_OFF();
            LED3_ON();
            break;
        default:
            LED1_OFF();
            LED2_OFF();
            LED3_OFF();
            break;
        }
        phase++;
        HAL_Delay(250);

        /* Every 4 phases (~1 s): sample the ADC1 internal channels. */
        if (phase % 4 == 0)
        {
            uint32_t now = HAL_GetTick();
            if (now - last_report >= 1000)
            {
                last_report = now;

                ADC_InternalResult adc;
                ADC_Internal_Sample(&adc);

                /* Actual supply voltage from VREFINT: VREFINT code = VREF/Vdda * 4095. */
                uint32_t vdda_mv = 0;
                if (adc.raw_vrefint != 0U)
                {
                    vdda_mv = (VREFINT_TYPICAL_MV * 4095UL) / adc.raw_vrefint;
                }

                /* Junction temperature from the factory-calibrated temp sensor. */
                int temp_c = TempC_FromCode(adc.raw_temp, vdda_mv);

                /* VBAT voltage: on F407 the VBAT channel measures VBAT/2 internally,
                 * so scale by 2. */
                uint32_t vbat_mv = (vdda_mv * adc.raw_vbat) / 4095UL * 2UL;

                printf("blink: LED cycle %lu @ %lu ms\r\n",
                       (unsigned long)(phase / 4), (unsigned long)now);
                printf("ADC1: temp=%hu code, VREFINT=%hu code, VBAT=%hu code\r\n",
                       adc.raw_temp, adc.raw_vrefint, adc.raw_vbat);
                printf("     Vdda ~= %lu mV, chip temp ~= %d C, VBAT ~= %lu mV\r\n",
                       (unsigned long)vdda_mv, temp_c, (unsigned long)vbat_mv);
            }
        }
    }

    return 0;
}
