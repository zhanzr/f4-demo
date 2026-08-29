#ifndef DEV1_F407_EE_FLASH_H
#define DEV1_F407_EE_FLASH_H

#include <stdint.h>

#define EE_FLASH_SIZE 256U

void EE_Flash_Init(void);
void EE_Flash_Erase(uint8_t value);
void EE_Flash_Program(const uint8_t *data);
void EE_Flash_Read(uint8_t *data);

#endif