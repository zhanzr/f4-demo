/**
  * @file    board.c
  * @brief   Board init for the STM32F407VET6 "nano-f407" board.
  *
  * Clock tree (HSE = 8 MHz):
  *   PLLM=8   -> PLL input  1 MHz
  *   PLLN=336 -> VCO      336 MHz
  *   PLLP=2   -> SYSCLK   168 MHz
  *   PLLQ=7   -> 48 MHz    (USB/SDIO)
  *   AHB=168 MHz, APB1=42 MHz (/4), APB2=84 MHz (/2)
  *   Flash latency 5 wait states, regulator scale 1.
  *
  * SystemClock_Config() is declared weak so an individual project can bring
  * in its own clock setup (e.g. blink_hello_24MHz runs at 24 MHz) without
  * affecting the default 168 MHz used by every other project.
  */

#include "board.h"
#include "uart_printf.h"
#include "swv_printf.h"

/* ------------------------------------------------------------------------ */
__attribute__((weak)) void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM       = 8U;
    RCC_OscInitStruct.PLL.PLLN       = 336U;
    RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ       = 7U;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
    {
        Error_Handler();
    }
}

/* ------------------------------------------------------------------------ */
static void GPIO_LED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* LED PB0: push-pull output, low = ON. Start OFF (high). */
    GPIO_InitStruct.Pin   = LED1_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    LED_OFF();
}

/* ------------------------------------------------------------------------ */
void Board_Init(void)
{
    SystemClock_Config();
    GPIO_LED_Init();
    UART_Init();
    SWV_Init();
}

/* ------------------------------------------------------------------------ */
void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
        /* Blink the LED as a fatal-error indicator. */
        LED_TOGGLE();
        for (volatile uint32_t i = 0; i < 1000000UL; i++) { }
    }
}
