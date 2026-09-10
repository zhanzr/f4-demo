/*
  math_shim.c - provide sqrt() for the vendored touch.c in this project.

  The touch calibration (TP_Adjust) computes sqrt() on uint32 squared
  distances only, for ratio checks (0.95..1.05). A plain integer square root
  is more than enough precision, and defining sqrt here keeps the vendored
  libm (whose ARM-state sqrt breaks the Thumb veneer under --gc-sections in
  the SDRAM app.ld link) out of the build entirely.
*/

#include <stdint.h>

double sqrt(double x)
{
    uint64_t n;
    uint64_t r, r0, r1;

    if (x <= 0.0 || x > 1e15)
    {
        return 0.0;
    }

    /* transform to an integer by converting the double mantissa: simplest
     * robust route is Newton on the integer part of x. */
    r0 = (uint64_t)x;
    if (r0 == 0U) { return 0.0; }
    n = (r0 > 0xFFFFU) ? 0x10000U : r0;

    /* Newton's method on integer sqrt: r = (r + n/r)/2 */
    r1 = n;
    do
    {
        r = r1;
        r1 = (r + n / r) / 2U;
    } while (r1 != r && r1 != r - 1U && r1 != r + 1U);

    return (double)r1;
}