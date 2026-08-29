#ifndef DEV1_F407_SPI_FLASH_H
#define DEV1_F407_SPI_FLASH_H

#include <stdint.h>

/* dev1-f407 has no SDRAM, so the test buffers live in internal SRAM (128 KB).
 * 4 KiB keeps both write+read buffers (8 KiB total) comfortably inside SRAM
 * alongside the stack/heap. */
#define SPI_FLASH_TEST_SIZE (4UL * 1024UL)

void SPI_Flash_Init(void);
uint32_t SPI_Flash_ReadJedecId(void);
void SPI_Flash_Erase64K(uint32_t address);
void SPI_Flash_PageProgram(uint32_t address, const uint8_t *data, uint32_t size);
void SPI_Flash_Read(uint32_t address, uint8_t *data, uint32_t size);

#endif