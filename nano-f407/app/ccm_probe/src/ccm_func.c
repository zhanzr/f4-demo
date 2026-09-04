/* Focused CCM-execution probe: the SIMPLEST possible code placed in CCM
 * (0x10000000). ccm_function is just `add r0,r1; bx lr`. A data marker is also
 * placed in CCM so the harness can prove (a) the D-bus data access to CCM works
 * and (b) the copy-in placed correct code bytes at the fetch address -- before
 * the harness attempts the call, isolating instruction-fetch as the suspect.
 */
#include <stdint.h>

__attribute__((section(".ram_code")))
uint32_t ccm_function(uint32_t a, uint32_t b)
{
    return a + b;
}
