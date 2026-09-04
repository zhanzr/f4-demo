/**
  * @file    clock_config.c
  * @brief   Project-local 48 MHz clock config for blink_hello_48m.
  *
  * The shared board layer (dev1-f407/board/board.c) provides a *weak*
  * SystemClock_Config() that runs every project at the default 168 MHz.
  * This project defines a *strong* SystemClock_Config() (48 MHz) that
  * overrides that weak default at link time, so ONLY this project runs at
  * 48 MHz -- all other dev1-f407 projects keep the 168 MHz default.
  *
  * Clock tree (HSE = 25 MHz):
  *   PLLM=25  -> PLL input  1 MHz
  *   PLLN=192 -> VCO       192 MHz
  *   PLLP=4   -> SYSCLK     48 MHz
  *   PLLQ=4   -> PLL48CLK  48 MHz  (USB/SDIO, not used here)
  *   AHB=48 MHz, APB1=24 MHz (/2), APB2=48 MHz (/1)
  *   Flash latency 1 wait state, regulator scale 1.
  *
  * PLLN=192 / PLLP=4 keeps the VCO at 192 MHz, inside the F407's 100-432 MHz
  * VCO range (a 48 MHz VCO would not lock reliably). APB1 is divided by 2 so
  * it stays within the F407's 42 MHz APB1 maximum.
  */

#include "board.h"
#include "stm32f4xx_hal.h"

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
    RCC_OscInitStruct.PLL.PLLN       = 192U;
    RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV4;
    RCC_OscInitStruct.PLL.PLLQ       = 4U;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
    {
        Error_Handler();
    }
}
