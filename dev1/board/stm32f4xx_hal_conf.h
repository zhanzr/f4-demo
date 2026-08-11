/**
  ******************************************************************************
  * @file    stm32f4xx_hal_conf.h
  * @brief   HAL configuration file for STM32F407VET6 (custom board).
  *          Only the modules actually used by these projects are enabled.
  ******************************************************************************
  */

#ifndef __STM32F4xx_HAL_CONF_H
#define __STM32F4xx_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/* ########################## Module Selection ############################## */
#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED

/* ########################## Oscillator Values ############################# */
#define HSE_VALUE    ((uint32_t)25000000U) /*!< Value of the External oscillator in Hz. Custom board: 25 MHz. */
#define HSE_STARTUP_TIMEOUT ((uint32_t)100U)
#define HSI_VALUE    ((uint32_t)16000000U)
#define LSI_VALUE    ((uint32_t)32000U)
#define LSE_VALUE    ((uint32_t)32768U)
#define LSE_STARTUP_TIMEOUT ((uint32_t)5000U)
#define CLOCK_CRYSTAL_OSC_ENABLE  1U
#define CLOCK_PLL_ENABLE          1U

/* ########################## System Configuration ########################## */
#define VDD_VALUE                    ((uint32_t)3300U)
#define TICK_INT_PRIORITY            ((uint32_t)0x0FU)
#define USE_RTOS                     0U
#define PREFETCH_ENABLE              1U
#define INSTRUCTION_CACHE_ENABLE     1U
#define DATA_CACHE_ENABLE            1U
#define EXTERNAL_CLOCK_VALUE         ((uint32_t)25000000U)

/* ############################# Assert Macro ############################### */
#ifdef USE_FULL_ASSERT
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
void assert_failed(uint8_t *file, uint32_t line);
#else
#define assert_param(expr) ((void)0U)
#endif

/* ############################# Includes ################################### */
/* Include the headers of the enabled modules (also pulls in stm32f4xx_hal_def.h
   and therefore stm32f4xx.h / core_cm4.h / <stdint.h>). */
#if defined(HAL_RCC_MODULE_ENABLED)
  #include "stm32f4xx_hal_rcc.h"
#endif /* HAL_RCC_MODULE_ENABLED */

#if defined(HAL_GPIO_MODULE_ENABLED)
  #include "stm32f4xx_hal_gpio.h"
#endif /* HAL_GPIO_MODULE_ENABLED */

#if defined(HAL_DMA_MODULE_ENABLED)
  #include "stm32f4xx_hal_dma.h"
#endif /* HAL_DMA_MODULE_ENABLED */

#if defined(HAL_CORTEX_MODULE_ENABLED)
  #include "stm32f4xx_hal_cortex.h"
#endif /* HAL_CORTEX_MODULE_ENABLED */

#if defined(HAL_FLASH_MODULE_ENABLED)
  #include "stm32f4xx_hal_flash.h"
#endif /* HAL_FLASH_MODULE_ENABLED */

#if defined(HAL_PWR_MODULE_ENABLED)
  #include "stm32f4xx_hal_pwr.h"
#endif /* HAL_PWR_MODULE_ENABLED */

#if defined(HAL_UART_MODULE_ENABLED)
  #include "stm32f4xx_hal_uart.h"
#endif /* HAL_UART_MODULE_ENABLED */

#ifdef __cplusplus
}
#endif

#endif /* __STM32F4xx_HAL_CONF_H */
