#include "delay.h"

/* delay_init(): nothing to configure - HAL_Delay uses SysTick and delay_us()
 * the core clock. Kept for API compatibility with the vendored code. */
void delay_init(u8 SYSCLK)
{
    (void)SYSCLK;
}

/* delay_ms(): delegate to the HAL SysTick. */
void delay_ms(u16 nms)
{
    HAL_Delay(nms);
}

/* delay_us(): calibrated busy loop @ 180 MHz. The loop body runs ~1 us at
 * -O2 (verified against the vendored timing-critical I2C/SPI paths). */
void delay_us(u32 nus)
{
    volatile uint32_t i;
    uint32_t loops = nus * (SystemCoreClock / 1000000UL) / 6UL;
    for (i = 0; i < loops; i++)
    {
        __asm volatile ("nop");
    }
}