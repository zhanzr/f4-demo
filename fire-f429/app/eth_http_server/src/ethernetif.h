/**
  * @file    eth_http_server/src/ethernetif.h
  * @brief   Ethernet network interface driver for lwIP (NO_SYS / raw API),
  *          fire-f429 board (RMII + LAN8720A PHY).
  */
#ifndef __ETHERNETIF_H__
#define __ETHERNETIF_H__

#include "stm32f4xx_hal.h"
#include "lwip/err.h"
#include "lwip/netif.h"

/* Exported functions ------------------------------------------------------ */
err_t ethernetif_init(struct netif *netif);
void  ethernetif_input(struct netif *netif);

/* Check the PHY link state and (re)start the MAC (call periodically). */
void ethernet_link_check_state(struct netif *netif);

/* Re-latch the RMII interface selection (MAC must be stopped). */
void eth_rmii_relatch(void);

#endif /* __ETHERNETIF_H__ */