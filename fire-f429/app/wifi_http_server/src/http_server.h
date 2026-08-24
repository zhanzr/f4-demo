/**
  * @file    wifi_http_server/src/http_server.h
  * @brief   lwIP raw-API HTTP server entry point.
  */
#ifndef __HTTP_SERVER_H__
#define __HTTP_SERVER_H__

/* Bind a listening TCP PCB on port 80 and install the accept callback.
 * Call after the netif is up (DHCP bound). */
void http_server_init(void);

#endif /* __HTTP_SERVER_H__ */