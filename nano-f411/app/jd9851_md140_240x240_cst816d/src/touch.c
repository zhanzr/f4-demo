/* touch.c - CST816D capacitive touch over bit-banged I2C (PA2 = SCL,
 * PA3 = SDA open-drain). Ported from the vendored TK018F3716 example's
 * touch_CTP.c: the touch data block is read from register 0x00; byte 3
 * (register 0x03) = 0x80 marks an active touch.
 *
 * NOTE: the F411 has no hardware I2C on PA2/PA3 (they are USART2 pins),
 * so the bus is bit-banged.
 */
#include "touch.h"
#include "stm32f4xx_hal.h"

/* 7-bit slave address 0x15; the byte on the wire is 0x2A for write. */
#define CTP_ADDR   0x2AU

#define I2C_SCL(x)  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, \
                     (x) ? GPIO_PIN_SET : GPIO_PIN_RESET)
#define I2C_SDA(x)  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, \
                     (x) ? GPIO_PIN_SET : GPIO_PIN_RESET)
#define I2C_SDA_READ()  (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_3) == GPIO_PIN_SET)

static void i2c_delay(void)
{
    volatile int d;
    for (d = 0; d < 20; d++) { }        /* a few hundred ns */
}

static void i2c_start(void)
{
    I2C_SDA(1);
    I2C_SCL(1);
    i2c_delay();
    I2C_SDA(0);                          /* SDA falls while SCL high */
    i2c_delay();
    I2C_SCL(0);
    i2c_delay();
}

static void i2c_stop(void)
{
    I2C_SDA(0);
    I2C_SCL(0);
    i2c_delay();
    I2C_SDA(1);
    I2C_SCL(1);
    i2c_delay();
}

static void i2c_ack(void)
{
    I2C_SCL(0);
    I2C_SDA(0);
    i2c_delay();
    I2C_SCL(1);
    i2c_delay();
    I2C_SCL(0);
}

static void i2c_noack(void)
{
    I2C_SCL(0);
    I2C_SDA(1);
    i2c_delay();
    I2C_SCL(1);
    i2c_delay();
    I2C_SCL(0);
}

static uint8_t i2c_wait_ack(void)
{
    uint16_t t = 0;

    I2C_SDA(1);                          /* release SDA */
    i2c_delay();
    I2C_SCL(1);
    i2c_delay();
    while (I2C_SDA_READ())
    {
        if (++t > 250)
        {
            i2c_stop();
            return 1U;                   /* no ACK */
        }
    }
    I2C_SCL(0);
    return 0U;
}

static void i2c_send_byte(uint8_t dat)
{
    I2C_SCL(0);
    for (int i = 0; i < 8; i++)
    {
        I2C_SDA((dat & 0x80U) != 0U);
        i2c_delay();
        I2C_SCL(1);
        i2c_delay();
        I2C_SCL(0);
        i2c_delay();
        dat <<= 1;
    }
}

static uint8_t i2c_read_byte(uint8_t ack)
{
    uint8_t receive = 0;

    I2C_SDA(1);                          /* release SDA for the read */
    for (int i = 0; i < 8; i++)
    {
        I2C_SCL(0);
        i2c_delay();
        I2C_SCL(1);
        receive = (uint8_t)(receive << 1);
        if (I2C_SDA_READ())
        {
            receive |= 1U;
        }
        i2c_delay();
    }

    if (ack == 0U)
    {
        i2c_noack();
    }
    else
    {
        i2c_ack();
    }

    return receive;
}

void Touch_Init(void)
{
    GPIO_InitTypeDef g;

    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA2 = SCL, push-pull output */
    g.Pin   = GPIO_PIN_2;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &g);

    /* PA3 = SDA, open-drain output (released high) */
    g.Pin   = GPIO_PIN_3;
    g.Mode  = GPIO_MODE_OUTPUT_OD;
    HAL_GPIO_Init(GPIOA, &g);

    I2C_SCL(1);
    I2C_SDA(1);
    i2c_delay();
    i2c_stop();                          /* release any stuck state */
}

/* Read len bytes starting at register 0 (the CST816 touch data block). */
void Touch_Read(uint8_t *buf, uint8_t len)
{
    i2c_start();
    i2c_send_byte((uint8_t)(CTP_ADDR & ~1U));    /* write: set reg ptr */
    if (i2c_wait_ack() != 0U) { i2c_stop(); return; }
    i2c_send_byte(0x00U);                        /* register 0x00 */
    if (i2c_wait_ack() != 0U) { i2c_stop(); return; }
    i2c_stop();

    i2c_start();
    i2c_send_byte((uint8_t)(CTP_ADDR | 1U));     /* read */
    if (i2c_wait_ack() != 0U) { i2c_stop(); return; }
    for (uint8_t i = 0; i < len; i++)
    {
        buf[i] = i2c_read_byte((uint8_t)(i == (len - 1U) ? 0U : 1U));
    }
    i2c_stop();
}