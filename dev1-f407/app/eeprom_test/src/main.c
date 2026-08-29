#include <stdio.h>
#include <stdint.h>
#include "board.h"
#include "ee_flash.h"

static uint8_t write_buffer[EE_FLASH_SIZE];
static uint8_t read_buffer[EE_FLASH_SIZE];

static uint32_t BytesPerSecond(uint32_t cycles)
{
    return (uint32_t)(((uint64_t)EE_FLASH_SIZE * SystemCoreClock) /
                      (uint64_t)cycles);
}

static void CapturePattern(void)
{
    for (uint32_t index = 0; index < EE_FLASH_SIZE; index++)
    {
        write_buffer[index] = (uint8_t)(index ^ (index >> 3) ^ 0xA5U);
    }
}

int main(void)
{
    uint32_t start;
    uint32_t erase_cycles;
    uint32_t program_cycles;
    uint32_t read_cycles;
    uint32_t errors = 0;

    HAL_Init();
    Board_Init();
    EE_Flash_Init();
    CapturePattern();

    printf("\r\n==== dev1-f407 AT24C02 EEPROM test ====\r\n");
    printf("I2C1: PB8=SCL, PB9=SDA, address 0x50, 400 kHz\r\n");

    start = DWT->CYCCNT;
    EE_Flash_Erase(0xFFU);
    erase_cycles = DWT->CYCCNT - start;

    start = DWT->CYCCNT;
    EE_Flash_Program(write_buffer);
    program_cycles = DWT->CYCCNT - start;

    start = DWT->CYCCNT;
    EE_Flash_Read(read_buffer);
    read_cycles = DWT->CYCCNT - start;

    for (uint32_t index = 0; index < EE_FLASH_SIZE; index++)
    {
        if (read_buffer[index] != write_buffer[index])
        {
            errors++;
        }
    }

    printf("Erase 256 B: %lu cycles\r\n", (unsigned long)erase_cycles);
    printf("Program 256 B: %lu cycles, %lu B/s\r\n",
           (unsigned long)program_cycles,
           (unsigned long)BytesPerSecond(program_cycles));
    printf("Read 256 B: %lu cycles, %lu B/s\r\n",
           (unsigned long)read_cycles,
           (unsigned long)BytesPerSecond(read_cycles));
    printf("Result: %s (%lu errors)\r\n",
           errors == 0U ? "PASS" : "FAIL", (unsigned long)errors);

    while (1)
    {
        LED1_TOGGLE();
        HAL_Delay(500);
    }
}