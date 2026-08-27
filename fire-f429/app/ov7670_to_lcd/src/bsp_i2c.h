#ifndef __BSP_I2C_H__
#define __BSP_I2C_H__
/****************************** Includes *****************************/
#include "stm32f4xx.h"
#include "stm32f4xx_hal.h"
#include "bsp_debug_usart.h"
/****************************** Defines *******************************/

#define I2C_OWN_ADDRESS           0x00
/* OV7670 SCCB: 7-bit address 0x21, 8-bit write address 0x42. */
#define OV7670_DEVICE_ADDRESS     0x42

/* 毫秒级延时(需要定时器支持)，或者重写Delay宏 */
#define Delay 		HAL_Delay

/* SCCB timing: ~50 us half-clock (the ALIENTEK reference timing, proven
 * with the OV7670). The OV7670's SCCB needs the CLASSIC two-phase transfer
 * (STOP between address-write and data-read) + this slow timing - the HW
 * I2C peripheral's repeated-start read does NOT work on this sensor. */
void I2CMaster_Init(void);              /* bit-bang SCCB GPIO init */

/* SCCB register access - OV7670 uses 8-bit register addresses. */
uint8_t OV7670_WriteReg(uint8_t Addr, uint8_t Data);
uint8_t OV7670_ReadReg(uint8_t Addr);
uint8_t OV7670_ProbeAddr(uint8_t addr);   /* 0 if the 8-bit addr ACKs */

#endif // __BSP_I2C_H__