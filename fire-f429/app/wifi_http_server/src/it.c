/* Interrupt handlers for the wifi_http_server app.
 *
 * The vendored scan_app/FreeRTOSConfig.h maps xPortPendSVHandler->PendSV_Handler
 * and vPortSVCHandler->SVC_Handler, so the GCC port.c exports those two vector
 * entries itself. We only need to point SysTick at the FreeRTOS tick handler.
 * The shared board stm32f4xx_it.c (HAL tick) is skipped (STM32_SKIP_BOARD_IT).
 */

#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

void xPortSysTickHandler(void);

/* The HAL starts the 1 kHz SysTick in HAL_Init() — before the FreeRTOS
 * scheduler runs. Forwarding those early ticks into xPortSysTickHandler()
 * with pxCurrentTCB == NULL crashes. Gate on a flag set right before
 * vTaskStartScheduler(). */
volatile uint32_t wiced_rtos_running = 0;

void SysTick_Handler(void)
{
    HAL_IncTick();   /* keep the HAL (uwTick) timeouts working */
    if (wiced_rtos_running != 0)
    {
        xPortSysTickHandler();
    }
}

/* Raw USART1 output for the fault dump (avoids libc/HAL, which may be the
 * thing that faulted). USART1: 0x40011000, TXE = SR bit7, DR = 0x04. */
static void FaultPutChar(char c)
{
    volatile uint32_t *sr = (volatile uint32_t *)0x40011000U;
    volatile uint32_t *dr = (volatile uint32_t *)0x40011004U;
    uint32_t guard = 1000000U;
    while (((*sr & 0x80U) == 0U) && (--guard != 0U)) { }
    *dr = (uint32_t)(uint8_t)c;
}

static void FaultPrint(const char *s)
{
    while (*s != '\0')
    {
        FaultPutChar(*s++);
    }
}

static void FaultPrintHex32(uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    char tmp[8];
    for (int i = 0; i < 8; i++)
    {
        tmp[i] = hex[(v >> (28 - 4 * i)) & 0xFU];
    }
    for (int i = 0; i < 8; i++)
    {
        FaultPutChar(tmp[i]);
    }
}

void HardFault_Dump(uint32_t pc, uint32_t msp)
{
    volatile uint32_t *stack = (volatile uint32_t *)msp;

    FaultPrint("\r\nHARDFAULT pc=");
    FaultPrintHex32(pc);
    FaultPrint(" r0=");
    FaultPrintHex32(stack[0]);
    FaultPrint(" r1=");
    FaultPrintHex32(stack[1]);
    FaultPrint(" r2=");
    FaultPrintHex32(stack[2]);
    FaultPrint(" r3=");
    FaultPrintHex32(stack[3]);
    FaultPrint(" r12=");
    FaultPrintHex32(stack[4]);
    FaultPrint(" lr=");
    FaultPrintHex32(stack[5]);
    FaultPrint(" cfsr=");
    FaultPrintHex32(SCB->CFSR);
    FaultPrint(" bfar=");
    FaultPrintHex32(SCB->BFAR);
    FaultPrint("\r\n");

    while (1)
    {
    }
}

/* Naked so no prologue shifts the stacked fault frame. */
__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "mrs r0, MSP\n"
        "ldr r1, [r0, #24]\n"
        "mov r2, r0\n"
        "b HardFault_Dump\n");
}