/**
  * @file    eth_http_server/src/app_ethernet.c
  * @brief   lwIP setup + DHCP state machine (NO_SYS / raw API).
  *          Based on the STM32F769I-Discovery eth_http app_ethernet.c.
  */

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"
#include "lwip/opt.h"
#include "main.h"
#if LWIP_DHCP
#include "lwip/dhcp.h"
#endif
#include "app_ethernet.h"
#include "ethernetif.h"
#include "http_server.h"

#include <stdio.h>

/* Private variables ---------------------------------------------------------*/
uint32_t EthernetLinkTimer;
static uint8_t server_started = 0;   /* HTTP listener brought up once */

/* Bring up the HTTP listener once an IP is available (DHCP or static). */
static void maybe_start_http_server(void)
{
  if (!server_started)
  {
    server_started = 1;
    http_server_start();
  }
}

#if LWIP_DHCP
#define MAX_DHCP_TRIES  4
uint32_t DHCPfineTimer = 0;
uint8_t DHCP_state = DHCP_OFF;
#endif

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  Notify the user about the network interface config status.
  */
void ethernet_link_status_updated(struct netif *netif)
{
  if (netif_is_link_up(netif))
  {
#if LWIP_DHCP
    DHCP_state = DHCP_START;
#else
    printf("ETH: link UP, static IP %s\r\n", ip4addr_ntoa(netif_ip4_addr(netif)));
#endif
  }
  else
  {
#if LWIP_DHCP
    DHCP_state = DHCP_LINK_DOWN;
#else
    printf("ETH: link DOWN\r\n");
#endif
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

#if LWIP_DHCP
/**
  * @brief  DHCP state machine.
  */
static void DHCP_Process(struct netif *netif)
{
  ip_addr_t ipaddr;
  ip_addr_t netmask;
  ip_addr_t gw;
  struct dhcp *dhcp;

  switch (DHCP_state)
  {
    case DHCP_START:
      printf("ETH: looking for DHCP server ...\r\n");
      ip_addr_set_zero_ip4(&netif->ip_addr);
      ip_addr_set_zero_ip4(&netif->netmask);
      ip_addr_set_zero_ip4(&netif->gw);
      dhcp_start(netif);
      DHCP_state = DHCP_WAIT_ADDRESS;
      break;

    case DHCP_WAIT_ADDRESS:
      if (dhcp_supplied_address(netif))
      {
        DHCP_state = DHCP_ADDRESS_ASSIGNED;
        printf("ETH: DHCP IP = %s\r\n", ip4addr_ntoa(netif_ip4_addr(netif)));
        maybe_start_http_server();
      }
      else
      {
        dhcp = (struct dhcp *)netif_get_client_data(netif, LWIP_NETIF_CLIENT_DATA_INDEX_DHCP);

        /* DHCP timeout -> fall back to a static IP. */
        if (dhcp != NULL && dhcp->tries > MAX_DHCP_TRIES)
        {
          DHCP_state = DHCP_TIMEOUT;

          IP_ADDR4(&ipaddr, IP_ADDR0, IP_ADDR1, IP_ADDR2, IP_ADDR3);
          IP_ADDR4(&netmask, NETMASK_ADDR0, NETMASK_ADDR1, NETMASK_ADDR2, NETMASK_ADDR3);
          IP_ADDR4(&gw, GW_ADDR0, GW_ADDR1, GW_ADDR2, GW_ADDR3);
          netif_set_addr(netif, &ipaddr, &netmask, &gw);
          printf("ETH: DHCP timeout, static IP %s\r\n", ip4addr_ntoa(netif_ip4_addr(netif)));
          maybe_start_http_server();
        }
      }
      break;

    case DHCP_LINK_DOWN:
      DHCP_state = DHCP_OFF;
      printf("ETH: cable not connected\r\n");
      break;

    default:
      break;
  }
}

/**
  * @brief  DHCP periodic check (every DHCP_FINE_TIMER_MSECS).
  */
void DHCP_Periodic_Handle(struct netif *netif)
{
  if (HAL_GetTick() - DHCPfineTimer >= DHCP_FINE_TIMER_MSECS)
  {
    DHCPfineTimer = HAL_GetTick();
    DHCP_Process(netif);
  }
}
#endif