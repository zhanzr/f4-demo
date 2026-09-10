#include <stdio.h>

#include "board.h"
#include "uart_printf.h"

/* Stage-2 app: migrated from bare/blink_hello, linked at 0xC0000000
 * (SDRAM bank 1) and booted there by the app/boot bootloader, which copies the
 * image from NAND offset 0. Prints the addresses of main() and a .bss
 * variable - both must lie inside the 0xC0000000.. SDRAM window - to verify
 * that the NAND->SDRAM remapping + jump worked. */

uint32_t app_bss_probe;
uint32_t app_data_probe = 0x11223344UL;

static void LED_Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = LED0_Pin | LED1_Pin;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);
    LED0_OFF();
    LED1_OFF();
}

int main(void)
{
    void (*main_ptr)(void) = (void (*)(void))main;

    HAL_Init();
    Board_Init();
    LED_Init();

    printf("\r\n=== apollo-f429 stage-2 app @ %lu Hz (from SDRAM 0xC0000000) ===\r\n",
           (unsigned long)SystemCoreClock);
    printf("remap check: &main=0x%08lX &app_bss_probe=0x%08lX\r\n",
           (unsigned long)(uintptr_t)main_ptr,
           (unsigned long)(uintptr_t)&app_bss_probe);

    while (1)
    {
        LED0_ON();
        LED1_OFF();
        HAL_Delay(250);
        LED0_OFF();
        LED1_ON();
        HAL_Delay(250);
    }
}