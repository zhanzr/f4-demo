/**
  * @file    bsp_i2c.c
  * @brief   SCCB (I2C-like) bus driver for the OV7670 on the fire-f429.
  *
  * The OV7670's SCCB needs the CLASSIC two-phase transfer (STOP between the
  * register-address write and the data read) + the slow ~50 us half-clock
  * timing - the STM32 HW I2C peripheral's repeated-start read
  * (HAL_I2C_Mem_Read with I2C_MEMADD_SIZE_8BIT) does NOT work on this
  * sensor (verified by app/ov7670_sccb_test: HW I2C returns 0xFE, the
  * bit-bang returns PID=0x76 VER=0x73).
  *
  * Ported from the ALIENTEK reference (扩展实验9 摄像头实验\HARDWARE\OV7670\
  * sccb.c) to the fire-f429 pins: SCL = PB6, SDA = PB7, 50 us half-clock.
  */
#include "bsp_i2c.h"

#define BB_SCL_PORT  GPIOB
#define BB_SCL_PIN   GPIO_PIN_6
#define BB_SDA_PORT  GPIOB
#define BB_SDA_PIN   GPIO_PIN_7

/* ~50 us delay at 180 MHz: each loop iteration is ~4 cycles, so 50 us =
 * 9000 cycles -> ~2250 iterations. (A little margin is fine - SCCB tolerates
 * up to ~4.7 us min high/low, 50 us half-clock is the ALIENTEK reference.) */
static void bb_delay(void)
{
    volatile uint32_t i = 2250;
    while (i) { i--; }
}

static void scl_hi(void) { HAL_GPIO_WritePin(BB_SCL_PORT, BB_SCL_PIN, GPIO_PIN_SET); }
static void scl_lo(void) { HAL_GPIO_WritePin(BB_SCL_PORT, BB_SCL_PIN, GPIO_PIN_RESET); }
static void sda_hi(void) { HAL_GPIO_WritePin(BB_SDA_PORT, BB_SDA_PIN, GPIO_PIN_SET); }
static void sda_lo(void) { HAL_GPIO_WritePin(BB_SDA_PORT, BB_SDA_PIN, GPIO_PIN_RESET); }
static uint8_t sda_read(void)
{
    return (HAL_GPIO_ReadPin(BB_SDA_PORT, BB_SDA_PIN) == GPIO_PIN_SET) ? 1u : 0u;
}

/**
  * @brief  Initialize the SCCB GPIO (bit-bang): SCL = PB6 push-pull,
  *         SDA = PB7 open-drain. Both idle high.
  */
void I2CMaster_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio.Pin   = BB_SCL_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BB_SCL_PORT, &gpio);

    gpio.Pin   = BB_SDA_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_OD;
    HAL_GPIO_Init(BB_SDA_PORT, &gpio);

    scl_hi();
    sda_hi();
}

/* ------------------------------------------------------------------ */
/* SCCB primitives (ALIENTEK reference timing/shape).                  */

static void sccb_start(void)
{
    sda_hi();
    scl_hi();
    bb_delay();
    sda_lo();
    bb_delay();
    scl_lo();
    bb_delay();
}

static void sccb_stop(void)
{
    sda_lo();
    bb_delay();
    scl_hi();
    bb_delay();
    sda_hi();
    bb_delay();
}

static void sccb_no_ack(void)
{
    bb_delay();
    sda_hi();
    scl_hi();
    bb_delay();
    scl_lo();
    bb_delay();
    sda_lo();
    bb_delay();
}

/* Write one byte; returns 0 on ACK, 1 on NACK. */
static uint8_t sccb_write_byte(uint8_t dat)
{
    uint8_t j;

    for (j = 0; j < 8; j++)
    {
        if (dat & 0x80U) { sda_hi(); } else { sda_lo(); }
        dat <<= 1;
        bb_delay();
        scl_hi();
        bb_delay();
        scl_lo();
    }

    /* 9th clock: release SDA to read the ACK */
    sda_hi();
    bb_delay();
    scl_hi();
    bb_delay();
    if (sda_read()) { scl_lo(); return 1; }   /* NACK */
    scl_lo();
    return 0;
}

/* Read one byte (master clocks; SDA released/open-drain). */
static uint8_t sccb_read_byte(void)
{
    uint8_t temp = 0, j;

    sda_hi();                     /* release SDA (open-drain = input) */
    for (j = 8; j; j--)
    {
        bb_delay();
        scl_hi();
        temp <<= 1;
        if (sda_read()) { temp++; }
        bb_delay();
        scl_lo();
    }
    return temp;
}

/* ------------------------------------------------------------------ */

/**
  * @brief  Probe one 8-bit address (start + address byte + stop).
  *         Used for in-context bus scans. Returns 0 if ACKed.
  */
uint8_t OV7670_ProbeAddr(uint8_t addr)
{
    sccb_start();
    if (sccb_write_byte(addr)) { sccb_stop(); return 1; }
    sccb_stop();
    return 0;
}

/**
  * @brief  Write one byte to an OV7670 register (8-bit register address).
  * @param  Addr: register address
  * @param  Data: value to write
  * @retval 0 = OK, 0xFF = NACK (sensor not responding)
  */
uint8_t OV7670_WriteReg(uint8_t Addr, uint8_t Data)
{
    uint8_t res = 0;

    sccb_start();
    if (sccb_write_byte(0x42)) { res = 1; }   /* device addr (write) */
    if (sccb_write_byte(Addr)) { res = 1; }   /* register addr       */
    if (sccb_write_byte(Data)) { res = 1; }   /* data                */
    sccb_stop();
    return res;
}

/**
  * @brief  Read one byte from an OV7670 register (8-bit register address).
  * @param  Addr: register address
  * @retval the register value, or 0xFF if the sensor didn't respond
  */
uint8_t OV7670_ReadReg(uint8_t Addr)
{
    uint8_t val = 0xFF;

    /* phase 1: write the register address */
    sccb_start();
    if (sccb_write_byte(0x42)) { sccb_stop(); return 0xFF; }
    bb_delay();
    if (sccb_write_byte(Addr)) { sccb_stop(); return 0xFF; }
    bb_delay();

    /* SCCB stop between address and data phases (required by this sensor) */
    sccb_stop();
    bb_delay();

    /* phase 2: read the data byte */
    sccb_start();
    if (sccb_write_byte(0x43)) { sccb_stop(); return 0xFF; }  /* dev (read) */
    bb_delay();
    val = sccb_read_byte();
    sccb_no_ack();
    sccb_stop();

    return val;
}

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/