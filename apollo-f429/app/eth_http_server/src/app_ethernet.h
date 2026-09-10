/**
  * @file    eth_http_server/src/app_ethernet.h
  * @brief   lwIP setup (NO_SYS / raw API), static IP.
  */
#ifndef __APP_ETHERNET_H__
#define __APP_ETHERNET_H__

#include "stm32f4xx_hal.h"
#include "lwip/opt.h"
#include "lwip/netif.h"

/* Link state change callback (registered as the netif link callback). */
void ethernet_link_status_updated(struct netif *netif);

/* Call periodically (every ~100 ms) to re-check the PHY link state. */
void Ethernet_Link_Periodic_Handle(struct netif *netif);

#endif /* __APP_ETHERNET_H__ */