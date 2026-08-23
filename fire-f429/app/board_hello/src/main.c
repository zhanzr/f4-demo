/*
 * board_hello - fire-f429 (STM32F429IGT6) board self-test @ 180 MHz.
 *
 * Reads and reports the on-board sensors:
 *   - ADC1 internal channels: VREFINT / die temperature / VBAT
 *   - PA4 (ADC1_IN4): GL5516 light-dependent resistor, raw mV
 *       [VDD] <=> GL5516 <=> PA4 <=> 10 kOhm <=> GND
 *   - DHT11 temperature/humidity on PE2 (single-wire)
 *   - MPU6050 6-axis accel/gyro on I2C1 (PB6=SCL, PB7=SDA, INT=PI1)
 * plus the LED blink.
 */
#include <stdio.h>
#include "board.h"
#include "adc_internal.h"
#include "dht11.h"
#include "mpu6050.h"

/* VREFINT typical value (1.18..1.24 V, typ 1.21). */
#define VREFINT_TYPICAL_MV   1210U

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

static void ReportADC(void)
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

    printf("ADC : VREFINT=%hu (%lu mV), temp=%hu code (%d C), VBAT=%hu (%lu mV)\r\n",
           adc.raw_vrefint, (unsigned long)vdda_mv,
           adc.raw_temp, temp_c,
           adc.raw_vbat, (unsigned long)vbat_mv);

    /* PA4 GL5516 light sensor: raw code + mV. */
    printf("LDR : PA4 raw=%hu, %lu mV\r\n",
           adc.raw_ldr, (unsigned long)adc.ldr_mv);
}

static void ReportDHT11(void)
{
    DHT11_Result dht;

    if (DHT11_Read(&dht) && dht.valid)
    {
        printf("DHT11: %u.%u C, %u.%u %%RH\r\n",
               dht.t_int, dht.t_dec, dht.rh_int, dht.rh_dec);
    }
    else
    {
        printf("DHT11: read failed\r\n");
    }
}

static void ReportMPU6050(void)
{
    MPU6050_Data mpu;

    if (MPU6050_Read(&mpu))
    {
        printf("MPU60: accel %6d %6d %6d, gyro %6d %6d %6d\r\n",
               mpu.ax, mpu.ay, mpu.az, mpu.gx, mpu.gy, mpu.gz);
    }
    else
    {
        printf("MPU60: read failed\r\n");
    }
}

int main(void)
{
    HAL_Init();
    Board_Init();
    ADC_Internal_Init();
    DHT11_Init();

    printf("\r\n==== fire-f429 (STM32F429IGT6) board_hello @ 180 MHz ====\r\n");
    printf("SYSCLK = %lu Hz (%lu MHz)\r\n",
           (unsigned long)SystemCoreClock,
           (unsigned long)(SystemCoreClock / 1000000UL));
    printf("pointers: data=%p bss=%p _sdata=%p _edata=%p _sbss=%p _ebss=%p\r\n",
            (void *)&app_data_probe, (void *)&app_bss_probe,
            (void *)_sdata, (void *)_edata, (void *)_sbss, (void *)_ebss);

    if (MPU6050_Init())
    {
        printf("MPU6050: found (WHO_AM_I 0x68)\r\n");
    }
    else
    {
        printf("MPU6050: NOT found\r\n");
    }

    uint32_t phase = 0;
    uint32_t last_report = 0;

    while (1)
    {
        /* Use the standalone PD12 LED for the default blink path. */
        LED_R_OFF();
        LED_G_OFF();
        LED_B_OFF();
        LED_1_TOGGLE();
        phase++;
        HAL_Delay(250);

        /* Every 4 phases (~1 s): sample + report the sensors. */
        if (phase % 4 == 0)
        {
            uint32_t now = HAL_GetTick();
            if (now - last_report >= 1000)
            {
                last_report = now;
                ReportADC();
                ReportDHT11();
                ReportMPU6050();
            }
        }
    }

    return 0;
}
