/**
  * @file    eth_http_server/src/main.c
  * @brief   Minimal HTTP server on the apollo-f429 (lwIP NO_SYS raw API,
  *          RMII + LAN8720A PHY, static IP). Serves the bundled e_server site.
  *
  *   HAL_Init + Board_Init (180 MHz, USART1 console, LEDs, SDRAM)
  *   PCF8574_Init (releases shared PB12 for the DHT11; also the ETH_RESET
  *     expander driver does its own init inside LAN8720_Init)
  *   lwip_init + Netif_Config + http_server_hw_init
  *   main loop: ethernetif_input + sys_check_timeouts + link periodic
  *
  * The ETH DMA descriptors/RX buffers live in internal SRAM (.sram_dma) -
  * the F429 has no D-cache, so CPU<->DMA stay coherent with no cache
  * maintenance and no special memory layout.
  *
  * Stage-2 (SDRAM/NAND): the bootloader owns the clock tree, SystemInit is
  * non-destructive (system_app.c), the whole app runs from SDRAM.
  */

#include "board.h"
#include "stm32f4xx_hal.h"
#include "main.h"
#include "lwip/opt.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"
#include "netif/etharp.h"
#include "ethernetif.h"
#include "app_ethernet.h"
#include "http_server.h"

/* Global network interface */
struct netif gnetif;

/* ------------------------------------------------------------------------ */
static void Netif_Config(void)
{
    ip_addr_t ipaddr;
    ip_addr_t netmask;
    ip_addr_t gw;

    /* Static IP (see main.h): board 192.168.5.200, host 192.168.5.99. */
    IP4_ADDR(&ipaddr, IP_ADDR0, IP_ADDR1, IP_ADDR2, IP_ADDR3);
    IP4_ADDR(&netmask, NETMASK_ADDR0, NETMASK_ADDR1, NETMASK_ADDR2, NETMASK_ADDR3);
    IP4_ADDR(&gw, GW_ADDR0, GW_ADDR1, GW_ADDR2, GW_ADDR3);

    netif_add(&gnetif, &ipaddr, &netmask, &gw, NULL, &ethernetif_init, &ethernet_input);
    netif_set_default(&gnetif);

#if LWIP_NETIF_LINK_CALLBACK
    netif_set_link_callback(&gnetif, ethernet_link_status_updated);
#endif
}

/* ------------------------------------------------------------------------ */
int main(void)
{
    HAL_Init();
    Board_Init();          /* 180 MHz, LEDs, USART1 console, SDRAM */

    printf("\r\n=== eth_http_server on apollo-f429 (LAN8720A, RMII) ===\r\n");
    printf("HTTP server: http://192.168.5.200/  (static IP)\r\n");

    lwip_init();
    Netif_Config();
    http_server_hw_init();   /* LEDs + sensors only; the listener comes up
                              * on link-up (see app_ethernet.c). */

    char ip_str[16];

    while (1)
    {
        ethernetif_input(&gnetif);        /* poll RX */
        sys_check_timeouts();             /* lwIP timers */
#if LWIP_NETIF_LINK_CALLBACK
        Ethernet_Link_Periodic_Handle(&gnetif);
#endif

        ip4addr_ntoa_r(netif_ip4_addr(&gnetif), ip_str, sizeof(ip_str));
        (void)ip_str;

        HAL_Delay(1);
    }
}