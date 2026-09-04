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

/* Time the CoreMark loops with the HAL SysTick 1 kHz tick (HAL_GetTick()).
 * SysTick is enabled by HAL_Init()/HAL_InitTick() at main() and is the
 * standard, compiler-agnostic CoreMark timing method: HAL_GetTick() is a
 * call into the HAL and returns the ms counter, so no optimizer can reorder
 * or hoist an inline hardware register read the way it could with the DWT
 * CYCCNT on clang builds. ms resolution is plenty for a multi-10-s run.
 * (The earlier DWT CYCCNT timing was dropped: CYCCNT is 32 bits and wraps
 * every 2^32/168 MHz = 25.57 s; SRAM runs > 25.57 s under-reported elapsed
 * time by a full wrap period, inflating iterations/s.) */
#define GETMYTIME(_t) (*_t = HAL_GetTick())
#define MYTIMEDIFF(fin, ini) ((fin) - (ini))
#define TIMER_RES_DIVIDER 1
#define SAMPLE_TIME_IMPLEMENTATION 1
/* HAL_GetTick() advances at 1 kHz regardless of the CPU clock. */
#define EE_TICKS_PER_SEC (1000U / TIMER_RES_DIVIDER)

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
