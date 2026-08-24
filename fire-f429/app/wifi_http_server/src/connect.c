/*
 * connect_main(): join the configured AP (wifi_config.h) and obtain an IP
 * address via DHCP, then print it over USART1. The LwIP netif is exposed
 * (wifi_netif) so the HTTP server can report the LAN IP.
 *
 * Based on the pattern from the vendored SDK's scan_app/wifi_base_config.c:
 *   wwd_buffer_init -> wwd_management_wifi_on -> wwd_wifi_join (retry loop)
 *   -> netif_add(ethernetif_init/ethernet_input) -> dhcp_start -> wait BOUND
 *
 * Runs inside the startup FreeRTOS task, after tcpip_init() completed, so the
 * LwIP timers/DHCP state machine advance on the tcpip thread.
 *
 * Reliability notes (measured on hardware):
 *  - The WPA2 handshake to a weak AP (-60 dBm) often fails with EAPOL M3
 *    timeouts; the join retries until it succeeds (or gives up).
 *  - Power save is disabled after wifi_on: with power save the module sleeps
 *    and misses the broadcast DHCP OFFER/ACK frames.
 *  - DHCP can still time out on a weak link; if it does, the whole
 *    join+DHCP sequence is restarted from scratch.
 */
#include "wwd_management.h"
#include "wwd_wifi.h"
#include "wwd_debug.h"
#include "wwd_assert.h"
#include "wwd_network.h"
#include "platform/wwd_platform_interface.h"
#include "RTOS/wwd_rtos_interface.h"
#include "wwd_buffer_interface.h"
#include "lwip/tcpip.h"
#include "lwip/dhcp.h"
#include "lwip/prot/dhcp.h"
#include "lwip/netif.h"
#include "netif/etharp.h"

#include <string.h>

#include "wifi_config.h"

#define JOIN_RETRY_DELAY_MS   (2000)
#define JOIN_MAX_RETRIES      (60)      /* ~2 min of join attempts */
#define DHCP_BIND_TIMEOUT_MS  (45000U)  /* DHCP give-up window, then rejoin */

/* Security of the AP from wifi_config.h (WPA2-PSK covers most home APs). */
#define AP_SEC   WICED_SECURITY_WPA2_MIXED_PSK

struct netif wifi_netif;                 /* exposed for the HTTP server */
static struct dhcp  netif_dhcp;

static const wiced_ssid_t ap_ssid =
{
    .length = (uint8_t)(sizeof(AP_SSID) - 1),
    .value  = AP_SSID,
};

/* Join the AP, retrying until it succeeds (or JOIN_MAX_RETRIES). Returns
 * WWD_SUCCESS when associated, an error code otherwise. */
static wwd_result_t join_ap(void)
{
    int retries = 0;

    while (wwd_wifi_join(&ap_ssid, AP_SEC, (const uint8_t *)AP_PASS,
                         (uint8_t)(sizeof(AP_PASS) - 1), NULL,
                         WWD_STA_INTERFACE) != WWD_SUCCESS)
    {
        retries++;
        if (retries >= JOIN_MAX_RETRIES)
        {
            WPRINT_APP_INFO(("Gave up joining " AP_SSID
                             " after %d tries - check the AP is on\n", retries));
            return WWD_UNFINISHED;
        }
        WPRINT_APP_INFO(("Failed to join " AP_SSID " .. retrying\n"));
        host_rtos_delay_milliseconds(JOIN_RETRY_DELAY_MS);
    }
    return WWD_SUCCESS;
}

/* Bring up the LwIP netif and run DHCP until bound (or timeout). */
static int dhcp_obtain_ip(void)
{
    ip4_addr_t ipaddr, netmask, gw;
    uint32_t   waited = 0;

    ip4_addr_set_zero(&gw);
    ip4_addr_set_zero(&ipaddr);
    ip4_addr_set_zero(&netmask);

    memset(&wifi_netif, 0, sizeof(wifi_netif));
    if (NULL == netif_add(&wifi_netif, &ipaddr, &netmask, &gw,
                          (void *)WWD_STA_INTERFACE,
                          ethernetif_init, ethernet_input))
    {
        WPRINT_APP_INFO(("Failed to start network interface\n"));
        return 0;
    }
    netif_set_default(&wifi_netif);
    netif_set_up(&wifi_netif);

    WPRINT_APP_INFO(("Obtaining IP address via DHCP\n"));
    dhcp_set_struct(&wifi_netif, &netif_dhcp);
    dhcp_start(&wifi_netif);

    while (netif_dhcp.state != DHCP_STATE_BOUND)
    {
        host_rtos_delay_milliseconds(10);
        waited += 10;
        if (waited >= DHCP_BIND_TIMEOUT_MS)
        {
            WPRINT_APP_INFO(("DHCP not bound (state=%u) after %lu s - "
                             "leaving and retrying the join\r\n",
                             (unsigned)netif_dhcp.state,
                             (unsigned long)(waited / 1000)));
            return 0;
        }
    }
    return 1;
}

void connect_main(void)
{
    wwd_result_t result;

    WPRINT_APP_INFO(("Starting Wiced v" WICED_VERSION "\n"));

    wwd_buffer_init(NULL);
    result = wwd_management_wifi_on(WICED_COUNTRY_AUSTRALIA);
    if (result != WWD_SUCCESS)
    {
        WPRINT_APP_INFO(("Error %d while starting WICED!\n", (int)result));
    }

    /* Stay fully awake: power save makes the module miss EAPOL/beacon frames
     * and hurts the DHCP exchange on a weak link. */
    wwd_wifi_disable_powersave();

    while (1)
    {
        WPRINT_APP_INFO(("Joining : " AP_SSID "\n"));
        if (join_ap() != WWD_SUCCESS)
        {
            /* Out of retries: park here and keep trying every 20 s. */
            while (1)
            {
                host_rtos_delay_milliseconds(20000);
            }
        }
        WPRINT_APP_INFO(("Successfully joined : " AP_SSID "\n"));

        if (dhcp_obtain_ip())
        {
            WPRINT_APP_INFO(("Network ready IP: %s\r\n",
                             ip4addr_ntoa(netif_ip4_addr(&wifi_netif))));
            /* Stay connected; the lease is kept alive by the DHCP timers.
             * Returns so the caller can start the HTTP server. */
            return;
        }

        /* DHCP gave up - drop the association and retry everything. */
        dhcp_stop(&wifi_netif);
        netif_set_down(&wifi_netif);
        netif_remove(&wifi_netif);
        wwd_wifi_leave(WWD_STA_INTERFACE);
        host_rtos_delay_milliseconds(2000);
    }
}