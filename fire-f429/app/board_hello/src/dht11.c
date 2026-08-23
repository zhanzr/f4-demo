/*
 * DHT11 single-wire driver (PE2), ported from the 极客科技 8051 example
 * (51_DHT21.c) with DWT microsecond timing.
 */
#include "dht11.h"
#include "board.h"
#include "stm32f4xx_hal.h"

#define DHT_PORT   GPIOE
#define DHT_PIN    GPIO_PIN_2

#define DHT_SET()    HAL_GPIO_WritePin(DHT_PORT, DHT_PIN, GPIO_PIN_SET)
#define DHT_RESET()  HAL_GPIO_WritePin(DHT_PORT, DHT_PIN, GPIO_PIN_RESET)
#define DHT_READ()   HAL_GPIO_ReadPin(DHT_PORT, DHT_PIN)

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

/* Wait for the line to be low, with a timeout (~2 ms). Returns 0 on timeout. */
static int WaitLow(void)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t limit = 2U * (SystemCoreClock / 1000U);
    while (DHT_READ() != GPIO_PIN_RESET)
    {
        if ((DWT->CYCCNT - start) > limit) return 0;
    }
    return 1;
}

/* Wait for the line to be high, with a timeout (~2 ms). Returns 0 on timeout. */
static int WaitHigh(void)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t limit = 2U * (SystemCoreClock / 1000U);
    while (DHT_READ() != GPIO_PIN_SET)
    {
        if ((DWT->CYCCNT - start) > limit) return 0;
    }
    return 1;
}

void DHT11_Init(void)
{
    GPIO_InitTypeDef gpio;

    DWT_Init();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    gpio.Pin   = DHT_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_OD;   /* open-drain: module has pull-up */
    gpio.Pull  = GPIO_PULLUP;           /* plus internal pull-up fallback */
    gpio.Speed = GPIO_SPEED_HIGH;
    HAL_GPIO_Init(DHT_PORT, &gpio);

    DHT_SET();                          /* idle high */
}

int DHT11_Read(DHT11_Result *res)
{
    uint8_t data[5] = {0, 0, 0, 0, 0};
    int i, bit;

    if (res == NULL) return 0;

    /* ---- start signal: low >= 18 ms, then release ---- */
    DHT_RESET();
    HAL_Delay(20);
    DHT_SET();
    DelayUs(30);                        /* release 20-40 us */

    /* ---- sensor presence: 80 us low + 80 us high ---- */
    if (!WaitLow())  return 0;          /* no response pulse */
    if (!WaitHigh()) return 0;

    /* ---- 40 data bits, MSB first ---- */
    for (i = 0; i < 40; i++)
    {
        /* Each bit starts with a 50 us low phase. */
        if (!WaitLow()) return 0;           /* line goes low (bit start) */
        if (!WaitHigh()) return 0;          /* wait for the rising edge (end of 50 us low) */

        /* Sample ~40 us into the high phase:
         *   '0' high ~26-28 us -> already low  -> bit 0
         *   '1' high ~70 us    -> still high  -> bit 1 */
        DelayUs(40);
        bit = (DHT_READ() == GPIO_PIN_SET) ? 1 : 0;

        data[i / 8] = (uint8_t)((data[i / 8] << 1) | (uint8_t)bit);
    }

    DHT_SET();                          /* idle high */

    res->rh_int   = data[0];
    res->rh_dec   = data[1];
    res->t_int    = data[2];
    res->t_dec    = data[3];
    res->checksum = data[4];

    res->valid = ((uint8_t)(data[0] + data[1] + data[2] + data[3]) == data[4]);
    return res->valid;
}
