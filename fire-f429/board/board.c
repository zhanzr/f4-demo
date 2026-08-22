/**
  * @file    board.c
  * @brief   Board init for the fire-f429 board (STM32F429IGT6).
  *
  * Clock tree (HSE = 25 MHz) -> 180 MHz:
  *   PLLM=25   -> PLL input  1 MHz
  *   PLLN=360  -> VCO      360 MHz
  *   PLLP=2    -> SYSCLK   180 MHz
  *   PLLQ=7    -> 51.4 MHz  (USB/SDIO, not used here)
  *   AHB=180 MHz, APB1=45 MHz (/4), APB2=90 MHz (/2)
  *   Flash latency 6 wait states, regulator scale 1.
  */

#include "board.h"
#include "uart_printf.h"
#include "swv_printf.h"

/* ------------------------------------------------------------------------ */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM       = 25U;
    RCC_OscInitStruct.PLL.PLLN       = 360U;
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
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_6) != HAL_OK)
    {
        Error_Handler();
    }
}

/* ------------------------------------------------------------------------ */
static void GPIO_LED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* RGB LEDs PH10/PH11/PH12 + LED1 PD12: push-pull, low = ON. Start OFF. */
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    GPIO_InitStruct.Pin = LED_R_Pin | LED_G_Pin | LED_B_Pin;
    HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);
    LED_R_OFF();
    LED_G_OFF();
    LED_B_OFF();

    GPIO_InitStruct.Pin = LED_1_Pin;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
    LED_1_OFF();
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
        /* Blink the red LED as a fatal-error indicator. */
        LED_R_TOGGLE();
        for (volatile uint32_t i = 0; i < 1000000UL; i++) { }
    }
}