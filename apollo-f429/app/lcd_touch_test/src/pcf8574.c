/*
 * pcf8574.c - PCF8574T I/O expander over the bit-banged I2C (myiic, PH4/PH5).
 * Ported from the vendored ALIENTEK Apollo 实验33 (DHT11) example. PB12 is the
 * expander's INT output; a read here releases the line so the shared DHT11
 * data pin can be driven by the sensor.
 */
#include "pcf8574.h"
#include "sys_compat.h"
#include "myiic.h"
#include "stm32f4xx_hal.h"

uint8_t PCF8574_Init(void)
{
    uint8_t temp;
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* PB12 input pull-up (PCF8574 INT line). */
    gpio.Pin   = GPIO_PIN_12;
    gpio.Mode  = GPIO_MODE_INPUT;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);
    IIC_Init();

    /* Probe PCF8574 presence on the bus. */
    IIC_Start();
    IIC_Send_Byte(PCF8574_ADDR);
    temp = IIC_Wait_Ack();
    IIC_Stop();

    PCF8574_WriteOneByte(0xFF);          /* all port pins high by default */
    return temp;
}

uint8_t PCF8574_ReadOneByte(void)
{
    uint8_t temp = 0;

    IIC_Start();
    IIC_Send_Byte(PCF8574_ADDR | 0x01);  /* read mode */
    IIC_Wait_Ack();
    temp = IIC_Read_Byte(0);
    IIC_Stop();
    return temp;
}

void PCF8574_WriteOneByte(uint8_t DataToWrite)
{
    IIC_Start();
    IIC_Send_Byte(PCF8574_ADDR | 0x00);  /* write mode */
    IIC_Wait_Ack();
    IIC_Send_Byte(DataToWrite);
    IIC_Wait_Ack();
    IIC_Stop();
    delay_ms(10);
}

void PCF8574_WriteBit(uint8_t bit, uint8_t sta)
{
    uint8_t data = PCF8574_ReadOneByte();
    if (sta == 0U) { data &= (uint8_t)~(uint8_t)(1U << bit); }
    else           { data |= (uint8_t)(1U << bit); }
    PCF8574_WriteOneByte(data);
}

uint8_t PCF8574_ReadBit(uint8_t bit)
{
    uint8_t data = PCF8574_ReadOneByte();
    return (data & (1U << bit)) ? 1U : 0U;
}