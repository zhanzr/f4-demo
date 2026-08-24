/**
  * @file    eth_http_server/src/http_server.h
  * @brief   lwIP raw-API HTTP server.
  */
#ifndef __HTTP_SERVER_H__
#define __HTTP_SERVER_H__

/* On-board hardware used by the API (LED + sensors). No network dependency. */
void http_server_hw_init(void);

/* Bind a listening TCP PCB on port 80 and install the accept callback.
 * Call ONLY after the netif has an IP (DHCP bound or static fallback). */
void http_server_start(void);

#endif /* __HTTP_SERVER_H__ */