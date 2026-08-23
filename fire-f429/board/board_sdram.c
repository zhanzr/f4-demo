#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_sdram.h"

#define SDRAM_EARLY __attribute__((section(".early_bss")))

static SDRAM_HandleTypeDef SDRAM_EARLY sdram_handle;
static FMC_SDRAM_TimingTypeDef SDRAM_EARLY sdram_timing;
static FMC_SDRAM_CommandTypeDef SDRAM_EARLY sdram_command;

static void ConfigureSdramGpio(void)
{
    GPIO_InitTypeDef gpio = {0};

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

void Board_SDRAM_EarlyInit(void)
{
    ConfigureSdramGpio();

    sdram_handle.Instance = FMC_SDRAM_DEVICE;
    sdram_handle.Init.SDBank = FMC_SDRAM_BANK2;
    sdram_handle.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_8;
    sdram_handle.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_12;
    sdram_handle.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_16;
    sdram_handle.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
    sdram_handle.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_2;
    sdram_handle.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
    sdram_handle.Init.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_2;
    sdram_handle.Init.ReadBurst = FMC_SDRAM_RBURST_ENABLE;
    sdram_handle.Init.ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_0;

    sdram_timing.LoadToActiveDelay = 2;
    sdram_timing.ExitSelfRefreshDelay = 7;
    sdram_timing.SelfRefreshTime = 4;
    sdram_timing.RowCycleDelay = 7;
    sdram_timing.WriteRecoveryTime = 2;
    sdram_timing.RPDelay = 2;
    sdram_timing.RCDDelay = 2;

    (void)HAL_SDRAM_Init(&sdram_handle, &sdram_timing);

    sdram_command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK2;
    sdram_command.AutoRefreshNumber = 1;
    sdram_command.CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
    (void)HAL_SDRAM_SendCommand(&sdram_handle, &sdram_command, HAL_MAX_DELAY);
    for (volatile uint32_t delay = 0; delay < 100000U; delay++) { }

    sdram_command.CommandMode = FMC_SDRAM_CMD_PALL;
    (void)HAL_SDRAM_SendCommand(&sdram_handle, &sdram_command, HAL_MAX_DELAY);
    sdram_command.CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    sdram_command.AutoRefreshNumber = 2;
    (void)HAL_SDRAM_SendCommand(&sdram_handle, &sdram_command, HAL_MAX_DELAY);
    sdram_command.CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
    sdram_command.AutoRefreshNumber = 1;
    sdram_command.ModeRegisterDefinition = 0x0222U;
    (void)HAL_SDRAM_SendCommand(&sdram_handle, &sdram_command, HAL_MAX_DELAY);
    (void)HAL_SDRAM_ProgramRefreshRate(&sdram_handle, 1386U);
}
