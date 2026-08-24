/**
  * @file    eth_http_server/src/main.c
  * @brief   Minimal HTTP server on the fire-f429 (lwIP NO_SYS raw API,
  *          RMII + LAN8720A PHY + DHCP). Serves the bundled e_server site.
  *
  *   HAL_Init + Board_Init (180 MHz, USART1 console, LEDs, SDRAM)
  *   lwip_init + Netif_Config + http_server_init
  *   main loop: ethernetif_input + sys_check_timeouts + link/DHCP periodic
  *
  * The ETH DMA descriptors/RX buffers live in internal SRAM (.sram_dma) -
  * the F429 has no D-cache, so CPU<->DMA stay coherent with no cache
  * maintenance and no special memory layout.
  */

#include "board.h"
#include "stm32f4xx_hal.h"
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

#if LWIP_DHCP
    ip_addr_set_zero_ip4(&ipaddr);
    ip_addr_set_zero_ip4(&netmask);
    ip_addr_set_zero_ip4(&gw);
#else
    IP4_ADDR(&ipaddr, IP_ADDR0, IP_ADDR1, IP_ADDR2, IP_ADDR3);
    IP4_ADDR(&netmask, NETMASK_ADDR0, NETMASK_ADDR1, NETMASK_ADDR2, NETMASK_ADDR3);
    IP4_ADDR(&gw, GW_ADDR0, GW_ADDR1, GW_ADDR2, GW_ADDR3);
#endif

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

    printf("\r\n=== eth_http_server on fire-f429 (LAN8720A, RMII) ===\r\n");
    printf("HTTP server: http://<dhcp-ip>/  (DHCP enabled)\r\n");

    lwip_init();
    Netif_Config();
    http_server_hw_init();   /* LEDs + sensors only; the listener comes up
                              * after DHCP (see app_ethernet.c). */

    char ip_str[16];

    while (1)
    {
        ethernetif_input(&gnetif);        /* poll RX */
        sys_check_timeouts();             /* lwIP timers */
        http_stream_poll();               /* MJPEG stream frames */
#if LWIP_NETIF_LINK_CALLBACK
        Ethernet_Link_Periodic_Handle(&gnetif);
#endif
#if LWIP_DHCP
        DHCP_Periodic_Handle(&gnetif);
#endif

        ip4addr_ntoa_r(netif_ip4_addr(&gnetif), ip_str, sizeof(ip_str));
        (void)ip_str;

        HAL_Delay(1);
    }
}