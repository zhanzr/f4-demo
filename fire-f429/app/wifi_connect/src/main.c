/*
 * WiFi connect for fire-f429 (EMW1062 / AP6181 / BCM43362 via SDIO1).
 *
 * Flow:
 *   HAL_Init + Board_Init (180 MHz, USART1, LEDs)
 *   platform_init_mcu_infrastructure()   - WICED NVIC group 4 + GPIO IRQ mgr
 *   xTaskCreate(startup_thread) -> wiced_rtos_running = 1 -> vTaskStartScheduler()
 *   startup_thread: tcpip_init() -> connect_main() (join AP + DHCP, print IP)
 *
 * The AP credentials live in src/wifi_config.h (gitignored; copy from
 * wifi_config.h.example and fill in). See README.md.
 *
 * Note: the HAL starts the 1 kHz SysTick in HAL_Init(), long before the
 * FreeRTOS scheduler runs. SysTick_Handler (src/it.c) therefore gates
 * xPortSysTickHandler() on the wiced_rtos_running flag, which is set right
 * before vTaskStartScheduler(). Otherwise the pre-scheduler tick dereferences
 * a NULL pxCurrentTCB and HardFaults ~1 ms into startup.
 */
#include <stdio.h>
#include "board.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "lwip/tcpip.h"
#include "lwip/init.h"
#include "wwd_debug.h"

void platform_init_mcu_infrastructure(void);   /* WICED platform_init.h */
extern void connect_main(void);                /* src/connect.c */
extern volatile uint32_t wiced_rtos_running;   /* src/it.c */

static void tcpip_init_done(void *arg);
static void startup_thread(void *arg);

static TaskHandle_t startup_thread_handle;

int main(void)
{
    HAL_Init();
    Board_Init();

    /* WICED platform: interrupt priorities (group 4) + GPIO IRQ manager. */
    platform_init_mcu_infrastructure();

    printf("\r\n==== fire-f429 WiFi connect (EMW1062 / AP6181 / SDIO) ====\r\n");

    BaseType_t ret = xTaskCreate((TaskFunction_t)startup_thread,
                                 "app_thread",
                                 4096 / sizeof(portSTACK_TYPE),
                                 NULL,
                                 (UBaseType_t)1,
                                 &startup_thread_handle);
    if (ret == pdPASS)
    {
        wiced_rtos_running = 1;   /* allow SysTick -> xPortSysTickHandler */
        vTaskStartScheduler();
    }

    printf("FreeRTOS scheduler failed to start\r\n");
    while (1)
    {
    }
}

static void startup_thread(void *arg)
{
    SemaphoreHandle_t lwip_done_sema;

    UNUSED_PARAMETER(arg);

    WPRINT_APP_INFO(("Started FreeRTOS " tskKERNEL_VERSION_NUMBER "\n"));
    WPRINT_APP_INFO(("Starting LwIP " LWIP_VERSION_STRING "\n"));

    /* Signal when LwIP has finished initialising, then run the connect app. */
    lwip_done_sema = xSemaphoreCreateCounting(1, 0);
    if (lwip_done_sema == NULL)
    {
        WPRINT_APP_INFO(("Could not create LwIP init semaphore\n"));
        return;
    }

    tcpip_init(tcpip_init_done, lwip_done_sema);
    xSemaphoreTake(lwip_done_sema, portMAX_DELAY);

    connect_main();
}

static void tcpip_init_done(void *arg)
{
    SemaphoreHandle_t lwip_done_sema = (SemaphoreHandle_t)arg;
    xSemaphoreGive(lwip_done_sema);
}
