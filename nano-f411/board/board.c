/**
  * @file    board.c
  * @brief   Board init for the STM32F411CEU6 "nano-f411" board.
  *
  * Clock tree (HSE = 25 MHz):
  *   PLLM=25  -> PLL input  1 MHz
  *   PLLN=200 -> VCO      200 MHz
  *   PLLP=2   -> SYSCLK   100 MHz
  *   PLLQ=4   -> 50 MHz    (USB OTG FS wants 48 MHz; at 100 MHz SYSCLK the
  *                           200 MHz VCO gives 50 MHz from Q=4, which is out
  *                           of spec. None of these projects use USB. To get
  *                           a 48 MHz PLLQ the VCO must be 96/192 MHz, which
  *                           forces a 96 MHz SYSCLK instead.)
  *   AHB=100 MHz, APB1=50 MHz (/2), APB2=50 MHz (/2)
  *   Flash latency 3 wait states, regulator scale 1.
  *
  * SystemClock_Config() is declared weak so an individual project can bring
  * in its own clock setup without affecting the default 100 MHz used by
  * every other project.
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
    RCC_OscInitStruct.PLL.PLLM       = 25U;
    RCC_OscInitStruct.PLL.PLLN       = 200U;
    RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV2;
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
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
    {
        Error_Handler();
    }
}

/* ------------------------------------------------------------------------ */
static void GPIO_LED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* LED PC13: push-pull output, low = ON. Start OFF (high). */
    GPIO_InitStruct.Pin   = LED1_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    LED_OFF();
}

/* ------------------------------------------------------------------------ */
static void GPIO_Button_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* USER button PA0: pressed shorts the pin to GND. Input with no pull
     * (an external pull-up, or open drain, decides the idle level). */
    GPIO_InitStruct.Pin   = BTN_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

/* ------------------------------------------------------------------------ */
void Board_Init(void)
{
    SystemClock_Config();
    GPIO_LED_Init();
    GPIO_Button_Init();
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