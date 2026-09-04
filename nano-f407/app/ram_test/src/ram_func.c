/* Minimal function linked into SRAM2 (.ram_code) and copy-in'd from flash.
 * It runs entirely from SRAM2 when called. It accumulates a value and returns
 * a fixed sentinel so main can prove the RAM-resident code actually executed.
 */
#include <stdint.h>

__attribute__((section(".ram_code")))
uint32_t ram_hello(uint32_t seed)
{
    volatile uint32_t acc = seed;
    for (uint32_t i = 0; i < 10000U; i++)
    {
        acc = acc * 3U + 1U;
    }
    /* Sentinel chosen to be unmistakable if the code runs from SRAM2. */
    return acc + 0x52414D31U; /* "RAM1" */
}
