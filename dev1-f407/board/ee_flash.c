#include "ee_flash.h"
#include "board.h"
#include "stm32f4xx_hal_i2c.h"

/* dev1-f407 AT24C02 EEPROM on I2C1, PB8=SCL / PB9=SDA (AF4). A2/A1/A0 are
 * grounded, so the 7-bit device address is 0x50. */
#define EE_FLASH_ADDRESS (0x50U << 1)
#define EE_FLASH_PAGE_SIZE 8U
#define EE_FLASH_TIMEOUT 100U

static I2C_HandleTypeDef ee_i2c;

void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
    GPIO_InitTypeDef gpio = {0};
    (void)hi2c;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    /* PB8 = I2C1_SCL, PB9 = I2C1_SDA (AF4). */
    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &gpio);
}

void EE_Flash_Init(void)
{
    ee_i2c.Instance = I2C1;
    ee_i2c.Init.ClockSpeed = 400000U;
    ee_i2c.Init.DutyCycle = I2C_DUTYCYCLE_2;
    ee_i2c.Init.OwnAddress1 = 0U;
    ee_i2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    ee_i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    ee_i2c.Init.OwnAddress2 = 0U;
    ee_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    ee_i2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&ee_i2c) != HAL_OK)
    {
        Error_Handler();
    }
}

static void WritePage(uint8_t address, const uint8_t *data)
{
    if (HAL_I2C_Mem_Write(&ee_i2c, EE_FLASH_ADDRESS, address,
                          I2C_MEMADD_SIZE_8BIT, (uint8_t *)data,
                          EE_FLASH_PAGE_SIZE, EE_FLASH_TIMEOUT) != HAL_OK)
    {
        Error_Handler();
    }
    while (HAL_I2C_IsDeviceReady(&ee_i2c, EE_FLASH_ADDRESS, 1,
                                 EE_FLASH_TIMEOUT) != HAL_OK) { }
}

void EE_Flash_Erase(uint8_t value)
{
    uint8_t page[EE_FLASH_PAGE_SIZE];
    for (uint32_t index = 0; index < EE_FLASH_PAGE_SIZE; index++)
    {
        page[index] = value;
    }
    for (uint32_t address = 0; address < EE_FLASH_SIZE; address += EE_FLASH_PAGE_SIZE)
    {
        WritePage((uint8_t)address, page);
    }
}

void EE_Flash_Program(const uint8_t *data)
{
    for (uint32_t address = 0; address < EE_FLASH_SIZE; address += EE_FLASH_PAGE_SIZE)
    {
        WritePage((uint8_t)address, &data[address]);
    }
}

void EE_Flash_Read(uint8_t *data)
{
    if (HAL_I2C_Mem_Read(&ee_i2c, EE_FLASH_ADDRESS, 0U,
                         I2C_MEMADD_SIZE_8BIT, data, EE_FLASH_SIZE,
                         EE_FLASH_TIMEOUT) != HAL_OK)
    {
        Error_Handler();
    }
}