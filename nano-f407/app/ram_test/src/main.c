/* Minimal RAM-execution proof for the dev1-f407 board.
 * ram_hello() is linked into SRAM2 (.ram_code, 0x2001C000) and copy-in'd from
 * flash at startup; only the harness lives in flash. Running it proves the
 * CPU executes code from SRAM2 correctly (CoreMark-in-SRAM uses the same path).
 */
#include <stdio.h>
#include <stdint.h>
#include "board.h"

/* RAM-execution probes for the dev1-f407 board.
 * ccm_function() and ram_hello() are placed in section(".ram_code") and
 * copy-in'd from flash at startup. Which memory they land in depends on the
 * linker script: SRAM2 (stm32f407ram_test.ld) or CCM (stm32f407ram_test_ccm.ld).
 * The trivial ccm_function isolates the barest "can the CPU execute one
 * instruction from that memory" case; ram_hello is the heavier SRAM2 proof.
 */
#include <stdio.h>
#include <stdint.h>
#include "board.h"

/* Simplest possible CCM/SRAM-resident function: add two ints, return. */
extern uint32_t ccm_function(uint32_t a, uint32_t b);
/* Heavier resident function (loop) for the SRAM2 proof. */
extern uint32_t ram_hello(uint32_t seed);

int main(void)
{
    HAL_Init();
    Board_Init();

    printf("\r\n==== ram_test (STM32F407VET6): RAM-execution probes ====\r\n");
    printf("SYSCLK = %lu Hz\r\n", (unsigned long)SystemCoreClock);

    /* 1) Trivial resident function (ccm_function). */
    uint32_t s = ccm_function(7U, 5U);
    printf("ccm_function(7,5) = %lu (expect 12)\r\n", (unsigned long)s);
    printf("ccm_function: %s\r\n", (s == 12U) ? "OK" : "FAIL");

    /* 2) Heavier resident function with loop (ram_hello). */
    uint32_t r = ram_hello(1U);
    volatile uint32_t acc = 1U;
    for (uint32_t i = 0; i < 10000U; i++)
    {
        acc = acc * 3U + 1U;
    }
    uint32_t ref = acc + 0x52414D31U;
    printf("ram_hello(1) = 0x%08lX (ref 0x%08lX): %s\r\n",
           (unsigned long)r, (unsigned long)ref, (r == ref) ? "OK" : "FAIL");

    printf("ram_test done.\r\n");

    while (1)
    {
        /* spin -- capture via serial port */
    }
}
