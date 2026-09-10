#include <stdio.h>
#include "board.h"
#include "stm32f4xx_hal_sdram.h"

#define SDRAM_BASE       0xC0000000UL   /* FMC bank 1 (ALIENTEK Apollo)      */
#define SDRAM_SIZE_BYTES (32UL * 1024UL * 1024UL)   /* W9825G6KH: 32 MiB    */
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
    __HAL_RCC_FMC_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_HIGH;
    gpio.Alternate = GPIO_AF12_FMC;

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_2 | GPIO_PIN_3;   /* SDNWE/SDNE0/SDCKE0 */
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

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 |   /* A10/A11/A12 */
               GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_8 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOG, &gpio);
}

static HAL_StatusTypeDef SDRAM_Init(void)
{
    FMC_SDRAM_TimingTypeDef timing = {
        .LoadToActiveDelay = 2,
        .ExitSelfRefreshDelay = 8,
        .SelfRefreshTime = 6,
        .RowCycleDelay = 6,
        .WriteRecoveryTime = 2,
        .RPDelay = 2,
        .RCDDelay = 2
    };
    FMC_SDRAM_CommandTypeDef command = {0};

    hsdram.Instance = FMC_SDRAM_DEVICE;
    hsdram.Init.SDBank = FMC_SDRAM_BANK1;
    hsdram.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_9;  /* 512 cols */
    hsdram.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_13;       /* 8192 rows, A0-A12 */
    hsdram.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_16;
    hsdram.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
    hsdram.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_3;
    hsdram.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
    hsdram.Init.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_2;
    hsdram.Init.ReadBurst = FMC_SDRAM_RBURST_ENABLE;
    hsdram.Init.ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_1;

    if (HAL_SDRAM_Init(&hsdram, &timing) != HAL_OK)
    {
        return HAL_ERROR;
    }

    /* Power-up sequence (ALIENTEK Apollo): CLK -> 500 us -> PALL ->
     * 8x autorefresh -> load mode register. */
    command.CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
    command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
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
    command.AutoRefreshNumber = 8;
    if (HAL_SDRAM_SendCommand(&hsdram, &command, 1000) != HAL_OK)
    {
        return HAL_ERROR;
    }

    command.CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
    command.AutoRefreshNumber = 1;
    command.ModeRegisterDefinition = 0x0230;   /* burst 1, sequential, CAS3, single-write */
    if (HAL_SDRAM_SendCommand(&hsdram, &command, 1000) != HAL_OK)
    {
        return HAL_ERROR;
    }

    /* 8192 rows / 64 ms at 90 MHz SDCLK: (0.064s * 90e6 / 8192) - 20 = 683. */
    return HAL_SDRAM_ProgramRefreshRate(&hsdram, 683);
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

    printf("\r\n==== apollo-f429 SDRAM test ====\r\n");
    printf("W9825G6KH: 32 MiB, 16-bit, FMC clock %lu MHz\r\n", SDRAM_FMC_CLOCK);

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
        LED1_ON();
        HAL_Delay(100);
        LED1_OFF();
        HAL_Delay(900);
    }
}