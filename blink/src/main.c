#include <stdio.h>
#include "board.h"

int main(void)
{
    HAL_Init();
    Board_Init();

    printf("\r\n==== STM32F407VET6 blink (LED1/2/3 @ PE13/14/15) ====\r\n");
    printf("SYSCLK = %lu Hz (%lu MHz)\r\n",
           (unsigned long)SystemCoreClock,
           (unsigned long)(SystemCoreClock / 1000000UL));

    uint32_t phase = 0;
    uint32_t last_report = 0;

    while (1)
    {
        switch (phase % 4)
        {
        case 0:
            LED1_ON();
            LED2_OFF();
            LED3_OFF();
            break;
        case 1:
            LED1_OFF();
            LED2_ON();
            LED3_OFF();
            break;
        case 2:
            LED1_OFF();
            LED2_OFF();
            LED3_ON();
            break;
        default:
            LED1_OFF();
            LED2_OFF();
            LED3_OFF();
            break;
        }
        phase++;

        HAL_Delay(250);

        if (phase % 4 == 0)
        {
            uint32_t now = HAL_GetTick();
            if (now - last_report >= 2000)
            {
                last_report = now;
                printf("blink: LED cycle %lu @ %lu ms\r\n",
                       (unsigned long)(phase / 4), (unsigned long)now);
            }
        }
    }

    return 0;
}
