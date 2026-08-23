/*
 * MPU6050 6-axis accelerometer/gyroscope driver - I2C1 (PB6=SCL, PB7=SDA),
 * INT on PI1.
 *
 * Ported from the Wildfire (野火) F429 "47-加速度陀螺仪—MPU6050" example
 * (StdPeriph -> HAL). Uses the same I2C1 400 kHz peripheral as the board's
 * EEPROM driver.
 *
 * Config (same as the reference):
 *   PWR_MGMT_1   = 0x00   wake from sleep
 *   SMPLRT_DIV   = 0x07   sample rate divider (1 kHz / 8 = 125 Hz)
 *   CONFIG       = 0x06   DLPF_CFG = 6 (5 Hz digital low-pass)
 *   ACCEL_CONFIG = 0x01   AFS_SEL = 1  -> +/- 4 g  (8192 LSB/g)
 *   GYRO_CONFIG  = 0x18   FS_SEL = 3   -> +/- 2000 deg/s (16.4 LSB/(deg/s))
 */
#ifndef __MPU6050_H__
#define __MPU6050_H__

#include <stdint.h>

typedef struct {
    int16_t ax, ay, az;   /* accelerometer, +/-4g  -> 8192 LSB/g   */
    int16_t gx, gy, gz;   /* gyroscope,     +/-2000dps -> 16.4 LSB/dps */
} MPU6050_Data;

/* Initialises I2C1 + MPU6050, checks WHO_AM_I. Returns 1 on success. */
int MPU6050_Init(void);

/* Reads accelerometer (6 bytes from 0x3B) and gyroscope (6 bytes from 0x43)
 * into out. Returns 1 on success. */
int MPU6050_Read(MPU6050_Data *out);

#endif /* __MPU6050_H__ */
