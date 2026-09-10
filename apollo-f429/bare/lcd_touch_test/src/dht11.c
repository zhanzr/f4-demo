/*
 * DHT11 single-wire driver (PB12), ported from the vendored ALIENTEK Apollo
 * "实验33 DHT11温湿度传感器实验" with DWT microsecond timing. Mirrors the
 * vendor pin handling: push-pull output to drive the start pulse, then switch
 * the pin to input mode to read the response + 40 data bits (the DHT11 module
 * has its own external pull-up). board.h: DHT11_Pin = GPIO_PIN_12 on GPIOB.
 */
#include "dht11.h"
#include "board.h"
#include "sys_compat.h"
#include "stm32f4xx_hal.h"

#define DHT_PORT   GPIOB
#define DHT_PIN    GPIO_PIN_12

#define DHT11_IO_IN()   {DHT_PORT->MODER &= ~(0x3UL << (12 * 2)); DHT_PORT->MODER |= (0UL << (12 * 2));}
#define DHT11_IO_OUT()  {DHT_PORT->MODER &= ~(0x3UL << (12 * 2)); DHT_PORT->MODER |= (1UL << (12 * 2));}

#define DHT11_DQ_OUT    PBout(12)
#define DHT11_DQ_IN     PBin(12)

/* ------------------------- DWT microsecond delay ------------------------- */
static void DWT_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

static void DelayUs(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - start) < ticks) { }
}

/* ---- reset DHT11: pull low >= 18 ms, then release ---- */
static void DHT11_Rst(void)
{
    DHT11_IO_OUT();
    DHT11_DQ_OUT = 0;
    HAL_Delay(20);
    DHT11_DQ_OUT = 1;
    DelayUs(30);
}

/* ---- check response: 40-80 us low, then 40-80 us high; 0 = present ---- */
static int DHT11_Check(void)
{
    uint32_t retry = 0;

    DHT11_IO_IN();
    while (DHT11_DQ_IN && retry < 100)
    {
        retry++;
        DelayUs(1);
    }
    if (retry >= 100) { return 1; }      /* stayed high: no sensor */
    retry = 0;
    while (!DHT11_DQ_IN && retry < 100)
    {
        retry++;
        DelayUs(1);
    }
    if (retry >= 100) { return 1; }      /* stayed low: no high edge */
    return 0;
}

/* ---- read one bit: 50 us low, then high; 26-28 us high = '0', ~70 us = '1' -- */
static uint8_t DHT11_Read_Bit(void)
{
    uint32_t retry = 0;

    while (DHT11_DQ_IN && retry < 100)   /* wait low */
    {
        retry++;
        DelayUs(1);
    }
    retry = 0;
    while (!DHT11_DQ_IN && retry < 100)  /* wait high */
    {
        retry++;
        DelayUs(1);
    }
    DelayUs(40);
    return DHT11_DQ_IN ? 1U : 0U;
}

/* ---- read one byte, MSB first ---- */
static uint8_t DHT11_Read_Byte(void)
{
    uint8_t i, dat = 0;
    for (i = 0; i < 8; i++)
    {
        dat = (uint8_t)((dat << 1) | DHT11_Read_Bit());
    }
    return dat;
}

void DHT11_Init(void)
{
    GPIO_InitTypeDef gpio;

    DWT_Init();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio.Pin   = DHT_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;   /* push-pull to drive the start pulse */
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_HIGH;
    HAL_GPIO_Init(DHT_PORT, &gpio);

    DHT11_Rst();
    (void)DHT11_Check();                /* first check often fails; data reads retry */
}

int DHT11_Read(DHT11_Result *res)
{
    uint8_t buf[5] = {0, 0, 0, 0, 0};
    uint8_t i;
    int attempt;

    if (res == NULL) return 0;

    for (attempt = 0; attempt < 3; attempt++)
    {
        DHT11_Rst();
        if (DHT11_Check() == 0)
        {
            __disable_irq();            /* 40-bit sampling is timing-critical */
            for (i = 0; i < 5; i++)
            {
                buf[i] = DHT11_Read_Byte();
            }
            __enable_irq();
            DHT11_IO_OUT();
            DHT11_DQ_OUT = 1;

            if ((uint8_t)(buf[0] + buf[1] + buf[2] + buf[3]) == buf[4])
            {
                res->rh_int   = buf[0];
                res->rh_dec   = buf[1];
                res->t_int    = buf[2];
                res->t_dec    = buf[3];
                res->checksum = buf[4];
                res->valid    = 1;
                res->fail_stage = 0;
                return 1;
            }
            res->valid = 0;
            return 0;                   /* checksum bad */
        }
        res->fail_stage = (DHT11_DQ_IN) ? 1 : 2;  /* 1 no-low, 2 no-high */
    }
    return 0;
}