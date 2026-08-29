#include <stdio.h>
#include <stdint.h>
#include "board.h"
#include "spi_flash.h"

static uint8_t write_buffer[SPI_FLASH_TEST_SIZE];
static uint8_t read_buffer[SPI_FLASH_TEST_SIZE];

static uint32_t ThroughputKibPerSecondMilli(uint32_t cycles)
{
    return (uint32_t)(((uint64_t)SPI_FLASH_TEST_SIZE * SystemCoreClock * 1000ULL) /
                      ((uint64_t)cycles * 1024ULL));
}

int main(void)
{
    uint32_t start;
    uint32_t erase_cycles;
    uint32_t program_cycles;
    uint32_t read_cycles;
    uint32_t errors = 0;
    uint32_t jedec_id;

    HAL_Init();
    Board_Init();
    SPI_Flash_Init();

    for (uint32_t index = 0; index < SPI_FLASH_TEST_SIZE; index++)
    {
        write_buffer[index] = (uint8_t)(index ^ (index >> 8) ^ 0x5AU);
    }

    jedec_id = SPI_Flash_ReadJedecId();
    printf("\r\n==== dev1-f407 SPI flash test ====\r\n");
    printf("W25Q64 JEDEC ID: 0x%06lX, SPI2 clock: 42 MHz\r\n",
           (unsigned long)jedec_id);

    start = DWT->CYCCNT;
    SPI_Flash_Erase64K(0U);
    erase_cycles = DWT->CYCCNT - start;

    start = DWT->CYCCNT;
    SPI_Flash_PageProgram(0U, write_buffer, SPI_FLASH_TEST_SIZE);
    program_cycles = DWT->CYCCNT - start;

    start = DWT->CYCCNT;
    SPI_Flash_Read(0U, read_buffer, SPI_FLASH_TEST_SIZE);
    read_cycles = DWT->CYCCNT - start;

    for (uint32_t index = 0; index < SPI_FLASH_TEST_SIZE; index++)
    {
        if (read_buffer[index] != write_buffer[index])
        {
            errors++;
        }
    }

    printf("Erase 64 KiB: %lu cycles\r\n", (unsigned long)erase_cycles);
    {
        uint32_t program_rate = ThroughputKibPerSecondMilli(program_cycles);
        uint32_t read_rate = ThroughputKibPerSecondMilli(read_cycles);

        printf("Program %lu KiB: %lu cycles, %lu.%03lu KiB/s\r\n",
           (unsigned long)(SPI_FLASH_TEST_SIZE / 1024U),
           (unsigned long)program_cycles,
            (unsigned long)(program_rate / 1000U),
            (unsigned long)(program_rate % 1000U));
        printf("Read %lu KiB: %lu cycles, %lu.%03lu KiB/s\r\n",
           (unsigned long)(SPI_FLASH_TEST_SIZE / 1024U),
           (unsigned long)read_cycles,
            (unsigned long)(read_rate / 1000U),
            (unsigned long)(read_rate % 1000U));
    }
    printf("Result: %s (%lu errors)\r\n",
           errors == 0U ? "PASS" : "FAIL", (unsigned long)errors);

    while (1)
    {
        LED1_TOGGLE();
        HAL_Delay(500);
    }
}