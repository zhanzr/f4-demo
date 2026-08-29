/**
  * @file    eth_http_server/src/main.c
  * @brief   Minimal HTTP server on the dev1-f407 (lwIP NO_SYS raw API,
  *          RMII + DP83848 PHY + DHCP). Serves the bundled e_server site.
  *
  *   HAL_Init + Board_Init (168 MHz, USART3 console, LEDs)
  *   lwip_init + Netif_Config + http_server_init
  *   main loop: ethernetif_input + sys_check_timeouts + link/DHCP periodic
  *
  * The ETH DMA descriptors/RX buffers live in internal SRAM (.sram_dma) -
  * the F407 has no D-cache, so CPU<->DMA stay coherent with no cache
  * maintenance and no special memory layout.
  *
  * !!! BROKEN / UNRELIABLE !!!
  * The on-board DP83848 Ethernet interface on this dev1-f407 board is
  * NOT functional. The PHY is detected on MDIO (addr 1, ID 2000:5c90) and
  * the link negotiates, but the RMII link is unstable: it flaps between
  * 10M/100M, drops ~50% of frames, and the board cannot sustain a usable
  * TCP connection (HTTP times out). This was confirmed with BOTH this
  * firmware AND a known-good vendor reference project (pav2000/
  * stm32f407-dp83848) using the same pins - both fail identically.
  * Root cause is a hardware issue on the board (RMII 50 MHz clock / PHY
  * strap / wiring), NOT the software. Do not rely on this interface.
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
    Board_Init();          /* 168 MHz, LEDs, USART3 console */

    printf("\r\n=== eth_http_server on dev1-f407 (DP83848, RMII) ===\r\n");
    printf("HTTP server: http://192.168.5.201/  (static IP)\r\n");

    lwip_init();
    Netif_Config();
    http_server_hw_init();   /* LEDs + internal ADC sensors */

    /* The vendor example starts httpd_init() unconditionally - the TCP
     * listener does not depend on the link state. low_level_init already
     * brought the link up synchronously (vendor-style), so start the
     * server right away. */
    http_server_start();

    char ip_str[16];

    while (1)
    {
        ethernetif_input(&gnetif);        /* poll RX */
        sys_check_timeouts();             /* lwIP timers */
#if LWIP_NETIF_LINK_CALLBACK
        Ethernet_Link_Periodic_Handle(&gnetif);
#endif
#if LWIP_DHCP
        DHCP_Periodic_Handle(&gnetif);
#endif

        ip4addr_ntoa_r(netif_ip4_addr(&gnetif), ip_str, sizeof(ip_str));
        (void)ip_str;

        /* Debug: print RX/TX frame counts every ~2s to confirm the board
         * is receiving and transmitting frames (ARP/ping). */
        {
          static uint32_t last_t = 0;
          if (HAL_GetTick() - last_t >= 2000)
          {
            last_t = HAL_GetTick();
            printf("ETH: rx=%lu tx=%lu txerr=%lu\r\n",
                   (unsigned long)eth_rx_cnt, (unsigned long)eth_tx_cnt,
                   (unsigned long)eth_tx_err);
          }
        }

        HAL_Delay(1);
    }
}