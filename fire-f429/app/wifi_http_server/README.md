# WiFi HTTP server - fire-f429 (app, EMW1062/AP6181 via SDIO)

Joins a Wi-Fi access point with the on-board **EMW1062** module (compatible
with AP6181/BCM43362) over SDIO1, obtains an IP address via DHCP, then serves
the **e_server** single-page site + JSON API over **lwIP raw TCP** on port 80.
Uses the vendored WICED/WWD SDK in `../../drivers/wifi_ap6181`.

This project runs from flash/SRAM with `.data/.bss` in SDRAM (the standard app
memory model, `DATA_IN_ExtSDRAM`).

## Credentials (never committed)

The AP name / password live in `src/wifi_config.h`, which is covered by
`**/wifi_config.h` in the repo `.gitignore` and is **never pushed**. To set up
on a new machine:

```bash
cp src/wifi_config.h.example src/wifi_config.h   # then fill in your AP
```

## Wiring (SDIO1)

| Function | Pin |
| -------- | --- |
| SDIO_D0..D3 | PC8, PC9, PC10, PC11 |
| SDIO_CMD | PD2 |
| SDIO_CLK | PC12 |
| WLAN_WAKEUP_HOST (OOB IRQ) | PA0 |

## What happens

1. `main()` sets up the 180 MHz clock, UART, and the WICED platform
   (interrupt priorities, GPIO IRQ manager).
2. A FreeRTOS task starts LwIP, then `connect_main()`:
   - `wwd_management_wifi_on()` powers on the 802.11 device,
   - `wwd_wifi_join()` joins the AP (retries every second),
   - a LwIP netif is added and `dhcp_start()` runs until `DHCP_STATE_BOUND`,
   - the assigned IP is printed.
3. `http_server_init()` binds a raw-API TCP listener on port 80 and serves:

```
GET  /                 gzip page (Content-Encoding: gzip)
GET  /api/leds         {"leds":[0]}       (real LED_1/PD12 state)
POST /api/leds         body {"leds":[0]}  -> applies to the LED
GET  /api/adc          {"vrefint_mv":..,"temp_c":..,"vbat_v":..,
                        "motion_x":..,"motion_y":..,"motion_z":..,
                        "dht11_t":..,"dht11_h":..,"ts":..}
GET  /api/camera       {"source":"dvi","ready":0,"ts":..}  (placeholder)
GET  /api/info         {"arch","lan_ip","public_ip":null,"geo":null,
                        "weather":null}
GET  /public/<name>    raw image bytes (from the embedded_files[] table)
```

The `public_ip` / `geo` / `weather` fields are `null` (the page shows "N/A")
because the board has no HTTP/TLS client - the e_server reference backend
fills them from external lookups.

## Web assets

The bundled site (`src/web_assets.h`) is generated from the shared
`e_server/web` + `e_server/public` sources by `e_server/build_web.py` at
build time (a CMake custom command). Edit the site in `e_server/web` and
rebuild - the board picks it up automatically.

## Sensors

The `/api/adc` endpoint reads the real on-board sensors (same drivers as
`app/board_hello`):

- ADC1 internal channels: VREFINT / die temperature / VBAT
- MPU6050 6-axis accelerometer on I2C1 (PB6/PB7) -> `motion_x/y/z` in g
- DHT11 temperature/humidity on PE2 -> `dht11_t` / `dht11_h`

## Console output

```
==== fire-f429 WiFi HTTP server (EMW1062 / AP6181 / SDIO) ====
Started FreeRTOS V9.0.0
Starting LwIP 2.0.3
Starting Wiced v006.002.001
Joining : scanned_ap_name_xx
Successfully joined : scanned_ap_name_xx
Obtaining IP address via DHCP
Network ready IP: 192.168.x.x
HTTP server listening on :80
```

Point a browser at `http://192.168.x.x/`.

## Reliability notes (from wifi_connect)

- **Power save disabled** (`wwd_wifi_disable_powersave()`): with power save
  the module sleeps and misses the broadcast DHCP OFFER/ACK frames.
- **Patience + retries**: the join retries every 2 s (up to 60 tries); if
  DHCP does not bind within 45 s the association is dropped and the whole
  join + DHCP sequence restarts.
- The startup HardFault guard in `src/it.c` (SysTick gated on
  `wiced_rtos_running`) is required - see the wifi_connect README.

## Verified on hardware (2026-08-24)

Flashed via OpenOCD (ULINK2) and captured on the USART1 console (COM36):

```
==== fire-f429 WiFi HTTP server (EMW1062 / AP6181 / SDIO) ====
Started FreeRTOS V9.0.0
Starting LwIP 2.0.3
Starting Wiced v006.002.001
Joining : HIKVISION_AE4490_2G4
Successfully joined : HIKVISION_AE4490_2G4
Obtaining IP address via DHCP
Network ready IP: 192.168.5.93
HTTP server listening on :80
```

Board-side state verified via the SWD debugger:

- `wifi_netif`: IP 192.168.5.93, netmask 255.255.255.0, gw 192.168.5.1,
  MAC `55:ab:00:08:57:55` - correct.
- `tcp_listen_pcbs` -> PCB local port `0x0050` (80), state LISTEN - the
  HTTP server is listening.

**Known limitation**: the host on the same subnet could not reach the board
(ARP for 192.168.5.93 never resolved; ping/HTTP timed out). The module's
**uplink (TX) is marginal** - DHCP RX works (lease obtained) but sustained
TX to the AP/host does not deliver frames. This is the same AP6181 radio
marginality documented in the wifi_connect README (weak AP, EAPOL timeouts).
If the board is unreachable, try: moving it closer to the AP, checking the
antenna, or using a different AP. The `eth_http_server` project (Ethernet,
no WiFi) is the reliable alternative on this board.

## Build and flash

```bash
cmake -G Ninja -B build .
ninja -C build
ninja -C build flash
```

Requires an antenna connected to the module.