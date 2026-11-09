/**
  * @file    board.c
  * @brief   Board init for the Nucleo-F413ZH board (STM32F413ZHT6).
  *
  * Clock tree (HSI = 16 MHz - the Nucleo-144 has no HSE crystal; the
  * ST-LINK MCO depends on solder bridges, so the internal oscillator
  * is the reliable source):
  *   PLLM=16  -> PLL input  1 MHz
  *   PLLN=200 -> VCO      200 MHz
  *   PLLP=2   -> SYSCLK   100 MHz (F413 family maximum)
  *   PLLQ=5   -> 40 MHz    (kernel-clock limit is 48 MHz: Q=4 would
  *                           give 50 MHz and HAL rejects the config)
  *   AHB=100 MHz, APB1=50 MHz (/2, max), APB2=100 MHz (/1, max)
  *   Flash latency 3 wait states, regulator scale 1.
  *
  * LEDs (Nucleo-144): LD1=PB0 (green), LD2=PB7 (blue), LD3=PB14 (red),
  * all HIGH-active (pin SET = LED on).
  *
  * Console: USART3 PD8 (TX) / PD9 (RX) - the ST-LINK VCP wiring on the
  * Nucleo-144 (115200 8-N-1).
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

    /* HSI-driven PLL (the Nucleo-144 has no HSE crystal fitted; whether
     * the ST-LINK MCO actually reaches PH0 depends on solder bridges).
     * PLLQ MUST give <= 48 MHz kernel clock: with PLLM=16 the VCO is
     * 192 MHz and Q=4 -> 48 MHz exactly (Q=4 on a 200 MHz VCO would be
     * 50 MHz and HAL rejects it). */
    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM            = 16U;
    RCC_OscInitStruct.PLL.PLLN            = 200U;
    RCC_OscInitStruct.PLL.PLLP            = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ            = 5U;   /* 40 MHz <= 48 MHz limit */
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;   /*  50 MHz (max) */
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;   /* 100 MHz (max) */
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
    {
        Error_Handler();
    }
}

/* ------------------------------------------------------------------------ */
static void GPIO_LED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* LD1 = PB0, LD2 = PB7, LD3 = PB14: push-pull outputs, HIGH = ON.
     * Start all OFF (low). */
    GPIO_InitStruct.Pin   = LED1_Pin | LED2_Pin | LED3_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    LED1_OFF();
    LED2_OFF();
    LED3_OFF();
}

/* ------------------------------------------------------------------------ */
static void GPIO_Button_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* USER button B1 = PC13. On the Nucleo-144 pressing B1 pulls the pin
     * HIGH (to 3V3); input with pull-down so the idle level reads LOW. */
    GPIO_InitStruct.Pin   = BTN_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull  = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
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
        /* Blink all LEDs as a fatal-error indicator. */
        LED1_TOGGLE();
        LED2_TOGGLE();
        LED3_TOGGLE();
        for (volatile uint32_t i = 0; i < 1000000UL; i++) { }
    }
}
