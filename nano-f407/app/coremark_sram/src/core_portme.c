#include <stdio.h>
#include <stdlib.h>
#include "coremark.h"
#include "custom_def.h"
#include "utils.h"
#include "stm32f4xx_hal.h"

#if VALIDATION_RUN
volatile ee_s32 seed1_volatile = 0x3415;
volatile ee_s32 seed2_volatile = 0x3415;
volatile ee_s32 seed3_volatile = 0x66;
#endif
#if PERFORMANCE_RUN
volatile ee_s32 seed1_volatile = 0x0;
volatile ee_s32 seed2_volatile = 0x0;
volatile ee_s32 seed3_volatile = 0x66;
#endif
#if PROFILE_RUN
volatile ee_s32 seed1_volatile = 0x8;
volatile ee_s32 seed2_volatile = 0x8;
volatile ee_s32 seed3_volatile = 0x8;
#endif
volatile ee_s32 seed4_volatile = ITERATIONS;
volatile ee_s32 seed5_volatile = 0;

#define CORETIMETYPE uint32_t

/* Time the CoreMark loops with the ARM DWT cycle counter (CYCCNT): a free
 * 32-bit counter running at the CPU clock with no ISR and no per-tick cost.
 * A single register read in start_time()/stop_time() is the minimum possible
 * timing overhead. 2^32 cycles @ 168 MHz ~ 25.6 s, well above any CoreMark
 * run, so a plain modular (unsigned wrap) difference is exact.
 * Compare to the old SysTick-based HAL_GetTick() (1 kHz ISR + ms resolution). */
#define DWT_CTRL        (*(volatile uint32_t *)0xE0001000U)
#define DWT_CYCCNT      (*(volatile uint32_t *)0xE0001004U)
#define DEMCR           (*(volatile uint32_t *)0xE000EDFCU)
#define DEMCR_TRCENA    (1U << 24U)
#define DWT_CTRL_CYCCNTENA (1U << 0U)

#define GETMYTIME(_t) (*_t = DWT_CYCCNT)
#define MYTIMEDIFF(fin, ini) ((fin) - (ini))
#define TIMER_RES_DIVIDER 1
#define SAMPLE_TIME_IMPLEMENTATION 1
/* EE_TICKS_PER_SEC must equal the CPU clock (CYCCNT counts CPU cycles), so
 * CoreMark's CoreMark/MHz and CoreMark/sec are derived from raw cycle counts. */
#define EE_TICKS_PER_SEC (HAL_RCC_GetHCLKFreq() / TIMER_RES_DIVIDER)

static CORETIMETYPE start_time_val, stop_time_val;

void start_time(void)
{
    GETMYTIME(&start_time_val);
}

void stop_time(void)
{
    GETMYTIME(&stop_time_val);
}

CORE_TICKS get_time(void)
{
    CORE_TICKS elapsed = (CORE_TICKS)(MYTIMEDIFF(stop_time_val, start_time_val));
    return elapsed;
}

secs_ret time_in_secs(CORE_TICKS ticks)
{
    secs_ret retval = ((secs_ret)ticks) / (secs_ret)EE_TICKS_PER_SEC;
    return retval;
}

ee_u32 default_num_contexts = 1;

void portable_init(core_portable *p, int *argc, char *argv[])
{
    (void)argc;
    (void)argv;

    if (sizeof(ee_ptr_int) != sizeof(ee_u8 *)) {
        ee_printf("ERROR! Please define ee_ptr_int to a type that holds a pointer!\n");
    }
    if (sizeof(ee_u32) != 4) {
        ee_printf("ERROR! Please define ee_u32 to a 32b unsigned type!\n");
    }
    p->portable_id = 1;

    /* Enable the ARM DWT cycle counter (0-count start; free-running thereafter). */
    DEMCR      |= DEMCR_TRCENA;
    DWT_CYCCNT  = 0;
    DWT_CTRL   |= DWT_CTRL_CYCCNTENA;
}

void portable_fini(core_portable *p)
{
    p->portable_id = 0;
}

void *portable_malloc(ee_size_t size)
{
    return malloc(size);
}

void portable_free(void *p)
{
    free(p);
}
