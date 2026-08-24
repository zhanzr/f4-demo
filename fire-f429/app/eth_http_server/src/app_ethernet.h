/**
  * @file    eth_http_server/src/app_ethernet.h
  * @brief   lwIP setup + DHCP state machine (NO_SYS / raw API).
  */
#ifndef __APP_ETHERNET_H__
#define __APP_ETHERNET_H__

#include "stm32f4xx_hal.h"
#include "lwip/opt.h"
#include "lwip/netif.h"

/* Application-level DHCP state machine (the STM32 example's own enum). */
#if LWIP_DHCP
typedef enum
{
  DHCP_OFF = 0,
  DHCP_START,
  DHCP_WAIT_ADDRESS,
  DHCP_ADDRESS_ASSIGNED,
  DHCP_TIMEOUT,
  DHCP_LINK_DOWN
} DHCP_State_TypeDef;
#endif

/* Link state change callback (registered as the netif link callback). */
void ethernet_link_status_updated(struct netif *netif);

/* Call periodically (every ~100 ms) to re-check the PHY link state. */
void Ethernet_Link_Periodic_Handle(struct netif *netif);

/* Call periodically (every ~250 ms) to drive the DHCP state machine. */
void DHCP_Periodic_Handle(struct netif *netif);

#endif /* __APP_ETHERNET_H__ */