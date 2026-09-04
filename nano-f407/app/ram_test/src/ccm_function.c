/* Simplest possible CCM-resident function. Does nothing but integer math and
 * returns -- no loops, no external calls, no libc, no data access -- to isolate
 * whether the CPU can execute any instruction placed in CCM (0x10000000) on
 * this board. Built into section(".ram_code") so the same startup copy-in path
 * used by the SRAM2 test places it in CCM when linked with the CCM script.
 */
#include <stdint.h>

__attribute__((section(".ram_code")))
uint32_t ccm_function(uint32_t a, uint32_t b)
{
    return a + b;
}
