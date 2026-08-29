#include "spi_flash.h"
#include "board.h"
#include "stm32f4xx_hal_spi.h"

/* dev1-f407 W25Q64 SPI flash on SPI2:
 *   CS    -> PE3  (GPIO)
 *   SCK   -> PB10 (AF5, SPI2_SCK)
 *   MISO  -> PC2  (AF5, SPI2_MISO)
 *   MOSI  -> PC3  (AF5, SPI2_MOSI)  */
#define FLASH_CS_PORT GPIOE
#define FLASH_CS_PIN  GPIO_PIN_3

#define CMD_WRITE_ENABLE 0x06U
#define CMD_READ_STATUS  0x05U
#define CMD_JEDEC_ID     0x9FU
#define CMD_READ_DATA    0x03U
#define CMD_PAGE_PROGRAM 0x02U
#define CMD_ERASE_64K    0xD8U

#define STATUS_BUSY 0x01U
#define STATUS_WEL  0x02U
#define SPI_TIMEOUT 1000U

static SPI_HandleTypeDef spi_flash;

static void FlashSelect(void)
{
    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_RESET);
}

static void FlashDeselect(void)
{
    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_SET);
}

static void Transfer(const uint8_t *tx, uint8_t *rx, uint16_t size)
{
    if (HAL_SPI_TransmitReceive(&spi_flash, tx, rx, size, SPI_TIMEOUT) != HAL_OK)
    {
        Error_Handler();
    }
}

static uint8_t ReadStatus(void)
{
    uint8_t tx[2] = {CMD_READ_STATUS, 0xFFU};
    uint8_t rx[2] = {0};

    FlashSelect();
    Transfer(tx, rx, sizeof(tx));
    FlashDeselect();
    return rx[1];
}

static void WriteEnable(void)
{
    uint8_t command = CMD_WRITE_ENABLE;

    FlashSelect();
    if (HAL_SPI_Transmit(&spi_flash, &command, 1, SPI_TIMEOUT) != HAL_OK)
    {
        Error_Handler();
    }
    FlashDeselect();

    while ((ReadStatus() & STATUS_WEL) == 0U) { }
}

static void WaitReady(void)
{
    while ((ReadStatus() & STATUS_BUSY) != 0U) { }
}

static void SendAddress(uint8_t command, uint32_t address)
{
    uint8_t tx[4] = {
        command,
        (uint8_t)(address >> 16),
        (uint8_t)(address >> 8),
        (uint8_t)address
    };

    if (HAL_SPI_Transmit(&spi_flash, tx, sizeof(tx), SPI_TIMEOUT) != HAL_OK)
    {
        Error_Handler();
    }
}

void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)
{
    GPIO_InitTypeDef gpio = {0};
    (void)hspi;

    __HAL_RCC_SPI2_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    /* PB10 = SPI2_SCK, PC2 = SPI2_MISO, PC3 = SPI2_MOSI (AF5). */
    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOB, &gpio);

    gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* CS = PE3, push-pull output. */
    gpio.Pin = FLASH_CS_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(FLASH_CS_PORT, &gpio);
    FlashDeselect();
}

void SPI_Flash_Init(void)
{
    spi_flash.Instance = SPI2;
    spi_flash.Init.Mode = SPI_MODE_MASTER;
    spi_flash.Init.Direction = SPI_DIRECTION_2LINES;
    spi_flash.Init.DataSize = SPI_DATASIZE_8BIT;
    spi_flash.Init.CLKPolarity = SPI_POLARITY_LOW;
    spi_flash.Init.CLKPhase = SPI_PHASE_1EDGE;
    spi_flash.Init.NSS = SPI_NSS_SOFT;
    spi_flash.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
    spi_flash.Init.FirstBit = SPI_FIRSTBIT_MSB;
    spi_flash.Init.TIMode = SPI_TIMODE_DISABLE;
    spi_flash.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    spi_flash.Init.CRCPolynomial = 7U;

    if (HAL_SPI_Init(&spi_flash) != HAL_OK)
    {
        Error_Handler();
    }
}

uint32_t SPI_Flash_ReadJedecId(void)
{
    uint8_t tx[4] = {CMD_JEDEC_ID, 0xFFU, 0xFFU, 0xFFU};
    uint8_t rx[4] = {0};

    FlashSelect();
    Transfer(tx, rx, sizeof(tx));
    FlashDeselect();
    return ((uint32_t)rx[1] << 16) | ((uint32_t)rx[2] << 8) | rx[3];
}

void SPI_Flash_Erase64K(uint32_t address)
{
    WriteEnable();
    FlashSelect();
    SendAddress(CMD_ERASE_64K, address);
    FlashDeselect();
    WaitReady();
}

void SPI_Flash_PageProgram(uint32_t address, const uint8_t *data, uint32_t size)
{
    while (size != 0U)
    {
        uint32_t page_remaining = 256U - (address & 255U);
        uint32_t chunk = size < page_remaining ? size : page_remaining;

        WriteEnable();
        FlashSelect();
        SendAddress(CMD_PAGE_PROGRAM, address);
        if (HAL_SPI_Transmit(&spi_flash, data, (uint16_t)chunk, SPI_TIMEOUT) != HAL_OK)
        {
            Error_Handler();
        }
        FlashDeselect();
        WaitReady();

        address += chunk;
        data += chunk;
        size -= chunk;
    }
}

void SPI_Flash_Read(uint32_t address, uint8_t *data, uint32_t size)
{
    FlashSelect();
    SendAddress(CMD_READ_DATA, address);
    while (size != 0U)
    {
        uint16_t chunk = size > 65535UL ? 65535U : (uint16_t)size;
        if (HAL_SPI_Receive(&spi_flash, data, chunk, SPI_TIMEOUT) != HAL_OK)
        {
            Error_Handler();
        }
        data += chunk;
        size -= chunk;
    }
    FlashDeselect();
}