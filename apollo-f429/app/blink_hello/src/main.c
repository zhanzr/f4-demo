#include <stdio.h>
#include "board.h"
#include "adc_internal.h"

/* VREFINT typical value (1.18..1.24 V, typ 1.21). */
#define VREFINT_TYPICAL_MV   1210U

/* Stage-2 app: migrated from bare/blink_hello, linked at 0xC0000000
 * (SDRAM bank 1) and booted there by the app/boot bootloader, which copies the
 * image from NAND offset 0. Besides the original blink_hello behaviour (LED
 * blink, ADC internal-channel report) it prints the addresses of main() and a
 * .bss variable - both must lie inside the 0xC0000000.. SDRAM window - to
 * verify that the NAND->SDRAM remapping + jump worked. */

uint32_t app_data_probe = 0x12345678UL;
uint32_t app_bss_probe;
extern char _sdata[];
extern char _edata[];
extern char _sbss[];
extern char _ebss[];

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
    void (*main_ptr)(void) = (void (*)(void))main;

    HAL_Init();
    Board_Init();
    ADC_Internal_Init();

    printf("\r\n==== apollo-f429 stage-2 app @ %lu Hz (from SDRAM 0xC0000000) ====\r\n",
           (unsigned long)SystemCoreClock);
    printf("SYSCLK = %lu Hz (%lu MHz)\r\n",
           (unsigned long)SystemCoreClock,
           (unsigned long)(SystemCoreClock / 1000000UL));
    printf("remap check: &main=0x%08lX &app_data_probe=0x%08lX "
           "&app_bss_probe=0x%08lX\r\n",
           (unsigned long)(uintptr_t)main_ptr,
           (unsigned long)(uintptr_t)&app_data_probe,
           (unsigned long)(uintptr_t)&app_bss_probe);
    printf("pointers: _sdata=%p _edata=%p _sbss=%p _ebss=%p\r\n",
           (void *)_sdata, (void *)_edata, (void *)_sbss, (void *)_ebss);

    uint32_t phase = 0;
    uint32_t last_report = 0;

    while (1)
    {
        /* Blink both on-board LEDs in opposite phases (LED1 = PB0, LED0 = PB1). */
        if (phase & 1)
        {
            LED0_ON();
            LED1_OFF();
        }
        else
        {
            LED0_OFF();
            LED1_ON();
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