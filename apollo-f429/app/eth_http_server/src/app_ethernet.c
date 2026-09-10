/**
  * @file    eth_http_server/src/app_ethernet.c
  * @brief   lwIP setup + static IP (NO_SYS / raw API).
  *          Based on the STM32F769I-Discovery eth_http app_ethernet.c.
  */

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"
#include "lwip/opt.h"
#include "main.h"
#include "app_ethernet.h"
#include "ethernetif.h"
#include "http_server.h"

#include <stdio.h>

/* Private variables ---------------------------------------------------------*/
uint32_t EthernetLinkTimer;
static uint8_t server_started = 0;   /* HTTP listener brought up once */

/* Bring up the HTTP listener once the link is up (static IP). */
static void maybe_start_http_server(void)
{
  if (!server_started)
  {
    server_started = 1;
    http_server_start();
  }
}

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  Notify the user about the network interface config status.
  */
void ethernet_link_status_updated(struct netif *netif)
{
  if (netif_is_link_up(netif))
  {
    printf("ETH: link UP, static IP %s\r\n", ip4addr_ntoa(netif_ip4_addr(netif)));
    maybe_start_http_server();
  }
  else
  {
    printf("ETH: link DOWN\r\n");
  }
}

#if LWIP_NETIF_LINK_CALLBACK
/**
  * @brief  Ethernet link periodic check (every 100 ms).
  */
void Ethernet_Link_Periodic_Handle(struct netif *netif)
{
  if (HAL_GetTick() - EthernetLinkTimer >= 100)
  {
    EthernetLinkTimer = HAL_GetTick();
    ethernet_link_check_state(netif);
  }
}
#endif