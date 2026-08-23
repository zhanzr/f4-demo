#ifndef FIRE_F429_SPI_FLASH_H
#define FIRE_F429_SPI_FLASH_H

#include <stdint.h>

#define SPI_FLASH_TEST_SIZE (64UL * 1024UL)

void SPI_Flash_Init(void);
uint32_t SPI_Flash_ReadJedecId(void);
void SPI_Flash_Erase64K(uint32_t address);
void SPI_Flash_PageProgram(uint32_t address, const uint8_t *data, uint32_t size);
void SPI_Flash_Read(uint32_t address, uint8_t *data, uint32_t size);

#endif
