/**
  * @file    swv_printf.c
  * @brief   SWV/ITM (SWO) printf implementation for STM32F407.
  *
  * Enables the DWT and the ITM trace port, and provides a non-blocking
  * single-byte writer on ITM stimulus port 0. The trace stream is driven
  * out TRACESWO (PE2, AF0) by the TPIU once the host (probe-rs `itm swo`)
  * has configured it.
  */

#include "swv_printf.h"
#include "stm32f4xx_hal.h"

/* ------------------------------------------------------------------------ */
void SWV_Init(void)
{
    /* Enable trace (DWT / ITM / TPIU). */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    __DSB();
    __ISB();

    /* Free-running cycle counter (also useful for profiling). */
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    DWT->CYCCNT = 0UL;

    /* Unlock the ITM (access key). */
    ITM->LAR = 0xC5ACCE55UL;

    /* TraceBusID = 1, enable SWO and sync packets, enable the ITM. */
    ITM->TCR = ITM_TCR_TraceBusID_Msk | ITM_TCR_SWOENA_Msk
             | ITM_TCR_SYNCENA_Msk | ITM_TCR_ITMENA_Msk;

    /* Grant access to stimulus ports 0..3. */
    ITM->TPR = ITM_TPR_PRIVMASK_Msk;

    /* Enable stimulus port 0. */
    ITM->TER = (1UL << 0);
}

/* ------------------------------------------------------------------------ */
int SWV_PutChar(int ch)
{
    uint8_t c = (uint8_t)ch;

    /* Only write when ITM is enabled and stimulus port 0 is enabled. The
       FIFO wait is bounded: if no trace sink is attached (no probe running
       `itm swo`) the port never becomes ready, and we must NOT block the
       application - the character is simply dropped. */
    if (((ITM->TCR & ITM_TCR_ITMENA_Msk) != 0UL) &&
        ((ITM->TER & (1UL << 0)) != 0UL))
    {
        volatile uint32_t guard = 1000UL;
        while (ITM->PORT[0].u32 == 0UL)
        {
            if (--guard == 0UL)
            {
                break;
            }
        }
        ITM->PORT[0].u8 = c;
    }
    return ch;
}
