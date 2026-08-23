#include <stdio.h>
#include "board.h"
#include "stm32f4xx_hal_sdram.h"

#define SDRAM_BASE       0xD0000000UL
#define SDRAM_SIZE_BYTES (8UL * 1024UL * 1024UL)
#define SDRAM_WORDS      (SDRAM_SIZE_BYTES / sizeof(uint16_t))
#define SDRAM_FMC_CLOCK  90UL

static SDRAM_HandleTypeDef hsdram;

void HAL_SDRAM_MspInit(SDRAM_HandleTypeDef *hsdram_handle)
{
    GPIO_InitTypeDef gpio = {0};
    (void)hsdram_handle;

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_FMC_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF12_FMC;

    gpio.Pin = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOC, &gpio);

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 | GPIO_PIN_9 |
               GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &gpio);

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_7 | GPIO_PIN_8 |
               GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 |
               GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOE, &gpio);

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
               GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_11 | GPIO_PIN_12 |
               GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOF, &gpio);

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 |
               GPIO_PIN_8 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOG, &gpio);

    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOH, &gpio);
}

static HAL_StatusTypeDef SDRAM_Init(void)
{
    FMC_SDRAM_TimingTypeDef timing = {
        .LoadToActiveDelay = 2,
        .ExitSelfRefreshDelay = 7,
        .SelfRefreshTime = 4,
        .RowCycleDelay = 7,
        .WriteRecoveryTime = 2,
        .RPDelay = 2,
        .RCDDelay = 2
    };
    FMC_SDRAM_CommandTypeDef command = {0};

    hsdram.Instance = FMC_SDRAM_DEVICE;
    hsdram.Init.SDBank = FMC_SDRAM_BANK2;
    hsdram.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_8;
    hsdram.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_12;
    hsdram.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_16;
    hsdram.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
    hsdram.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_2;
    hsdram.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
    hsdram.Init.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_2;
    hsdram.Init.ReadBurst = FMC_SDRAM_RBURST_ENABLE;
    hsdram.Init.ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_0;

    if (HAL_SDRAM_Init(&hsdram, &timing) != HAL_OK)
    {
        return HAL_ERROR;
    }

    command.CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
    command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK2;
    command.AutoRefreshNumber = 1;
    if (HAL_SDRAM_SendCommand(&hsdram, &command, 1000) != HAL_OK)
    {
        return HAL_ERROR;
    }
    HAL_Delay(1);

    command.CommandMode = FMC_SDRAM_CMD_PALL;
    if (HAL_SDRAM_SendCommand(&hsdram, &command, 1000) != HAL_OK)
    {
        return HAL_ERROR;
    }

    command.CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    command.AutoRefreshNumber = 2;
    if (HAL_SDRAM_SendCommand(&hsdram, &command, 1000) != HAL_OK)
    {
        return HAL_ERROR;
    }

    command.CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
    command.AutoRefreshNumber = 1;
    command.ModeRegisterDefinition = 0x0222;
    if (HAL_SDRAM_SendCommand(&hsdram, &command, 1000) != HAL_OK)
    {
        return HAL_ERROR;
    }

    return HAL_SDRAM_ProgramRefreshRate(&hsdram, 1386);
}

static uint16_t Pattern(uint32_t index)
{
    return (uint16_t)(0x5AA5U ^ (uint16_t)(index * 251U));
}

int main(void)
{
    volatile uint16_t *memory = (volatile uint16_t *)SDRAM_BASE;
    uint32_t start;
    uint32_t write_cycles;
    uint32_t read_cycles;
    uint32_t errors = 0;

    HAL_Init();
    Board_Init();

    printf("\r\n==== fire-f429 SDRAM test ====\r\n");
    printf("IS42S16400J: 8 MiB, 16-bit, FMC clock %lu MHz\r\n", SDRAM_FMC_CLOCK);

    if (SDRAM_Init() != HAL_OK)
    {
        printf("SDRAM initialization FAILED\r\n");
        Error_Handler();
    }

    start = DWT->CYCCNT;
    for (uint32_t index = 0; index < SDRAM_WORDS; index++)
    {
        memory[index] = Pattern(index);
    }
    __DSB();
    write_cycles = DWT->CYCCNT - start;

    start = DWT->CYCCNT;
    for (uint32_t index = 0; index < SDRAM_WORDS; index++)
    {
        if (memory[index] != Pattern(index))
        {
            errors++;
        }
    }
    __DSB();
    read_cycles = DWT->CYCCNT - start;

    printf("Write: %lu cycles, %lu.%03lu MiB/s\r\n", (unsigned long)write_cycles,
            (unsigned long)(((SDRAM_SIZE_BYTES / (1024UL * 1024UL)) * SystemCoreClock) / write_cycles),
            (unsigned long)(((SDRAM_SIZE_BYTES / (1024UL * 1024UL)) * SystemCoreClock % write_cycles) * 1000UL / write_cycles));
    printf("Read:  %lu cycles, %lu.%03lu MiB/s\r\n", (unsigned long)read_cycles,
            (unsigned long)(((SDRAM_SIZE_BYTES / (1024UL * 1024UL)) * SystemCoreClock) / read_cycles),
            (unsigned long)(((SDRAM_SIZE_BYTES / (1024UL * 1024UL)) * SystemCoreClock % read_cycles) * 1000UL / read_cycles));
    printf("Result: %s (%lu errors)\r\n", errors == 0U ? "PASS" : "FAIL", (unsigned long)errors);

    while (1)
    {
        LED_G_ON();
        HAL_Delay(100);
        LED_G_OFF();
        HAL_Delay(900);
    }
}
