/* Step-by-step CCM root-cause probe for nano-f407 (STM32F407VET6).
 *
 * Isolates WHY executing code from CCM (0x10000000) bus-faults, by working up
 * the chain in one run, before ever calling the CCM function:
 *
 *   1) DATA write+read to a CCM address      -> proves the CPU data bus reaches CCM
 *   2) Code-byte readback at &ccm_function   -> proves the copy-in placed valid
 *                                                 instructions at the fetch address
 *   3) Call ccm_function() in CCM            -> demonstrates the instruction FETCH
 *                                                 itself is what fails (IBUSERR)
 *
 * If steps 1-2 pass but step 3 faults with BFSR.IBUSERR, then CCM is reachable
 * for data but NOT for instruction fetch on this part -- pointing at a known
 * STM32F4 hardware trait: the CCM RAM sits on the CPU D-bus only (data), so the
 * code/I-bus cannot fetch from 0x10000000.
 */
#include <stdio.h>
#include <stdint.h>
#include "board.h"

extern uint32_t ccm_function(uint32_t a, uint32_t b);

/* Fixed CCM data address, deliberately clear of the resident function page. */
#define CCM_DATA_ADDR ((volatile uint32_t *)0x10000100UL)

int main(void)
{
    HAL_Init();
    Board_Init();

    printf("\r\n==== ccm_probe (nano-f407): isolate CCM fetch ====\r\n");
    printf("ccm_function() at &0x%08lX\r\n",
           (unsigned long)(uintptr_t)&ccm_function);

    /* 1) Data bus reach: write then read a marker at a CCM address. */
    *CCM_DATA_ADDR = 0xDEADBEEFUL;
    uint32_t d = *CCM_DATA_ADDR;
    printf("[1] CCM data write/read @0x10000100: 0x%08lX -> %s\r\n",
           (unsigned long)d, (d == 0xDEADBEEFUL) ? "OK" : "FAIL");

    /* 2) Read back the instructions the copy-in placed at &ccm_function.
          For `add r0,r1; bx lr` expect an ADD (Thumb) then 0x4770 (BX LR). */
    const uint16_t *code = (const uint16_t *)&ccm_function;
    printf("[2] CCM code bytes @%08lX: 0x%04X 0x%04X 0x%04X 0x%04X\r\n",
           (unsigned long)(uintptr_t)code,
           (unsigned)code[0], (unsigned)code[1],
           (unsigned)code[2], (unsigned)code[3]);

    /* 3) The call: fetch from CCM. */
    printf("[3] calling ccm_function(7,5) in CCM...\r\n");
    fflush(stdout);
    uint32_t s = ccm_function(7U, 5U);
    printf("[3] ccm_function(7,5) = %lu (expect 12)\r\n", (unsigned long)s);
    printf("ccm_probe: CCM code execution => %s\r\n", (s == 12U) ? "WORKS" : "FAIL");

    while (1)
    {
    }
}
