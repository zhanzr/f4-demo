#include <stdio.h>
#include "board.h"
#include "custom_def.h"
#include "dhry.h"

int main(void)
{
    HAL_Init();
    Board_Init();

    const uint32_t cpu_hz = HAL_RCC_GetHCLKFreq();

    printf("\r\n=== Dhrystone 2.1 on STM32F411CEU6 @ %lu Hz ===\r\n",
           (unsigned long)cpu_hz);

    while (1)
    {
        dhry_main(cpu_hz);
        printf("\r\nCPU freq: %lu Hz (%lu MHz)\r\n",
               (unsigned long)cpu_hz, (unsigned long)(cpu_hz / 1000000UL));
        printf("Compiler: %s\r\n", COMPILER_NAME);
        HAL_Delay(10000);
    }

    return 0;
}
