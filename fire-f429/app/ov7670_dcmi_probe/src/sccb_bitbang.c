/* SCCB (I2C-like) bit-bang driver for the OV7670 bring-up.
 * Ported from the ALIENTEK reference (扩展实验9 摄像头实验\HARDWARE\OV7670\sccb.c)
 * to the fire-f429 pins: SCL = PB6, SDA = PB7 (same as I2C1), 50 us half-clock.
 * Open-drain SDA with internal pull-up; PB6/PB7 work as GPIO when the I2C
 * peripheral is not enabled. */

#include "sccb_bitbang.h"
#include "stm32f4xx_hal.h"

#define BB_SCL_PORT  GPIOB
#define BB_SCL_PIN   GPIO_PIN_6
#define BB_SDA_PORT  GPIOB
#define BB_SDA_PIN   GPIO_PIN_7

/* ~50 us delay at 180 MHz (calibrated: ~2250 iterations). */
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

void SCCB_BB_InitGPIO(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* SCL: push-pull output. */
    gpio.Pin   = BB_SCL_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BB_SCL_PORT, &gpio);

    /* SDA: open-drain output (readable). */
    gpio.Pin   = BB_SDA_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_OD;
    HAL_GPIO_Init(BB_SDA_PORT, &gpio);

    scl_hi();
    sda_hi();
}

void SCCB_BB_Start(void)
{
    sda_hi();
    scl_hi();
    bb_delay();
    sda_lo();
    bb_delay();
    scl_lo();
    bb_delay();
}

void SCCB_BB_Stop(void)
{
    sda_lo();
    bb_delay();
    scl_hi();
    bb_delay();
    sda_hi();
    bb_delay();
}

uint8_t SCCB_BB_WriteByte(uint8_t dat)
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

    /* 9th clock: release SDA -> ACK? */
    /* open-drain: drive high then release below via input mode is cleaner,
     * but with open-drain output driving high = released. */
    sda_hi();
    bb_delay();
    scl_hi();
    bb_delay();
    if (sda_read()) { scl_lo(); return SCCB_BB_ACK_FAIL; }
    scl_lo();
    return 0;
}

uint8_t SCCB_BB_ReadByte(void)
{
    uint8_t temp = 0, j;

    /* SDA released (open-drain high = input) */
    sda_hi();
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

uint8_t SCCB_BB_ReadReg(uint8_t reg)
{
    /* write phase: device addr (write) + reg */
    SCCB_BB_Start();
    if (SCCB_BB_WriteByte(0x42)) { SCCB_BB_Stop(); return SCCB_BB_ACK_FAIL; }
    bb_delay();
    if (SCCB_BB_WriteByte(reg))  { SCCB_BB_Stop(); return SCCB_BB_ACK_FAIL; }
    bb_delay();

    /* ALIENTEK-style: STOP between the register-address write and the
     * read phase. Pure SCCB (not I2C repeated-start); the OV7670 datasheet
     * and the reference driver both use this form. */
    SCCB_BB_Stop();
    bb_delay();

    /* read phase: device addr (read) + data + NA */
    SCCB_BB_Start();
    if (SCCB_BB_WriteByte(0x43)) { SCCB_BB_Stop(); return SCCB_BB_ACK_FAIL; }
    bb_delay();
    {
        uint8_t val = SCCB_BB_ReadByte();
        /* NA */
        sda_hi(); bb_delay(); scl_hi(); bb_delay(); scl_lo();
        SCCB_BB_Stop();
        return val;
    }
}

uint8_t SCCB_BB_WriteReg(uint8_t reg, uint8_t val)
{
    SCCB_BB_Start();
    if (SCCB_BB_WriteByte(0x42)) { SCCB_BB_Stop(); return SCCB_BB_ACK_FAIL; }
    bb_delay();
    if (SCCB_BB_WriteByte(reg))  { SCCB_BB_Stop(); return SCCB_BB_ACK_FAIL; }
    bb_delay();
    if (SCCB_BB_WriteByte(val))  { SCCB_BB_Stop(); return SCCB_BB_ACK_FAIL; }
    SCCB_BB_Stop();
    return 0;
}