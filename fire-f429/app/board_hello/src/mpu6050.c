/*
 * MPU6050 driver for fire-f429 (I2C1, PB6=SCL / PB7=SDA, INT=PI1).
 */
#include "mpu6050.h"
#include "board.h"
#include "stm32f4xx_hal_i2c.h"

#define MPU6050_ADDR          (0x68U << 1)   /* AD0 low, 7-bit << 1 */
#define MPU6050_TIMEOUT       100U

/* Registers */
#define MPU6050_RA_SMPLRT_DIV  0x19
#define MPU6050_RA_CONFIG      0x1A
#define MPU6050_RA_GYRO_CONFIG 0x1B
#define MPU6050_RA_ACCEL_CONFIG 0x1C
#define MPU6050_RA_ACCEL_XOUT_H 0x3B
#define MPU6050_RA_GYRO_XOUT_H  0x43
#define MPU6050_RA_PWR_MGMT_1   0x6B
#define MPU6050_RA_WHO_AM_I     0x75

static I2C_HandleTypeDef mpu_i2c;

/* I2C1 pins are shared with the board EEPROM; HAL calls this weak hook for
 * every I2C1 handle in this project. */
void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
    GPIO_InitTypeDef gpio = {0};
    (void)hi2c;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    gpio.Pin      = GPIO_PIN_6 | GPIO_PIN_7;   /* PB6=SCL, PB7=SDA */
    gpio.Mode     = GPIO_MODE_AF_OD;
    gpio.Pull     = GPIO_PULLUP;
    gpio.Speed    = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &gpio);
}

static void MPU_WriteReg(uint8_t reg, uint8_t val)
{
    HAL_I2C_Mem_Write(&mpu_i2c, MPU6050_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                      &val, 1U, MPU6050_TIMEOUT);
}

static void MPU_ReadRegs(uint8_t reg, uint8_t *buf, uint8_t n)
{
    HAL_I2C_Mem_Read(&mpu_i2c, MPU6050_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                     buf, n, MPU6050_TIMEOUT);
}

int MPU6050_Init(void)
{
    uint8_t id = 0;

    /* INT pin PI1: input (unused for basic polling reads). */
    __HAL_RCC_GPIOI_CLK_ENABLE();
    {
        GPIO_InitTypeDef gpio = {0};
        gpio.Pin  = GPIO_PIN_1;
        gpio.Mode = GPIO_MODE_INPUT;
        gpio.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(GPIOI, &gpio);
    }

    mpu_i2c.Instance             = I2C1;
    mpu_i2c.Init.ClockSpeed      = 400000U;
    mpu_i2c.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    mpu_i2c.Init.OwnAddress1     = 0U;
    mpu_i2c.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    mpu_i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    mpu_i2c.Init.OwnAddress2     = 0U;
    mpu_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    mpu_i2c.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&mpu_i2c) != HAL_OK)
    {
        return 0;
    }

    /* Wake + configure (same values as the Wildfire example). */
    MPU_WriteReg(MPU6050_RA_PWR_MGMT_1, 0x00);
    MPU_WriteReg(MPU6050_RA_SMPLRT_DIV, 0x07);
    MPU_WriteReg(MPU6050_RA_CONFIG,     0x06);
    MPU_WriteReg(MPU6050_RA_ACCEL_CONFIG, 0x01);   /* +/-4g   */
    MPU_WriteReg(MPU6050_RA_GYRO_CONFIG, 0x18);    /* +/-2000 */

    MPU_ReadRegs(MPU6050_RA_WHO_AM_I, &id, 1U);
    return (id == 0x68U) ? 1 : 0;
}

int MPU6050_Read(MPU6050_Data *out)
{
    uint8_t buf[6];

    if (out == NULL) return 0;

    MPU_ReadRegs(MPU6050_RA_ACCEL_XOUT_H, buf, 6U);
    out->ax = (int16_t)((buf[0] << 8) | buf[1]);
    out->ay = (int16_t)((buf[2] << 8) | buf[3]);
    out->az = (int16_t)((buf[4] << 8) | buf[5]);

    MPU_ReadRegs(MPU6050_RA_GYRO_XOUT_H, buf, 6U);
    out->gx = (int16_t)((buf[0] << 8) | buf[1]);
    out->gy = (int16_t)((buf[2] << 8) | buf[3]);
    out->gz = (int16_t)((buf[4] << 8) | buf[5]);

    return 1;
}
