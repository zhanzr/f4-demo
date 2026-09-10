/**
  * @file    system_app.c
  * @brief   Non-destructive system init for the apollo-f429 stage-2 app.
  *
  * The stock ST system_stm32f4xx.c SystemInit() re-configures the RCC clock
  * tree. That is not needed here: the app/boot bootloader already brought the
  * HSE 25 MHz PLL to 180 MHz and initialized the SDRAM the app's code and data
  * run from. Re-running a full clock init would be redundant (and with
  * DATA_IN_ExtSDRAM would even touch the FMC SDRAM, whose timing the bootloader
  * already nailed down while the SDRAM holds our stack).
  *
  * So the app provides its own SystemInit() that does nothing destructive, plus
  * a SystemCoreClock that reflects what the bootloader left (180 MHz core). The
  * bootloader owns the clock tree; the app only uses it.
  */

#include "stm32f4xx.h"

/* Bootloader left the core at 180 MHz (HSE 25 MHz, M25 N360 P2). */
uint32_t SystemCoreClock = 180000000U;

/* Referenced by HAL_RCC_GetHCLKFreq()/GetPCLKx()Freq(). Same tables as the
 * stock system_stm32f4xx.c (the clocks themselves are owned by the bootloader,
 * so only the tables are needed for the HAL's read-back math). */
const uint8_t AHBPrescTable[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 6, 7, 8, 9};
const uint8_t APBPrescTable[8]  = {0, 0, 0, 0, 1, 2, 3, 4};

/* Called by the reset handler before main. Deliberately leaves RCC/FMC
 * untouched, but enables the FPU (the app is built -mfloat-abi=hard). */
void SystemInit(void)
{
    SCB->CPACR |= ((3UL << 10 * 2) | (3UL << 11 * 2));   /* CP10+CP11 full access */
    SystemCoreClock = 180000000U;
}

/* HAL clock helpers may call this; the clock tree is owned by the bootloader,
 * so just report the known value. */
void SystemCoreClockUpdate(void)
{
    SystemCoreClock = 180000000U;
}