# Ethernet HTTP server - fire-f429 (app, LAN8720A PHY via RMII)

A minimal HTTP server on the **fire-f429** board's on-board **LAN8720A**
Ethernet PHY (RMII), using **lwIP 2.0.3 (NO_SYS / raw API)** and the
**HAL ETH** driver. It serves the same **e_server** single-page site + JSON
API as `app/wifi_http_server`, but over wired Ethernet - no WiFi module.

Ported from the STM32F769I-Discovery `eth_http` project and the vendor F429
`42-ETH-LWIP` LAN8720A examples.

## Wiring (RMII, fire-f429)

| Function       | Pin  |
| -------------- | ---- |
| RMII_REF_CLK   | PA1  |
| MDIO           | PA2  |
| MDC            | PC1  |
| CRS_DV         | PA7  |
| RXD0           | PC4  |
| RXD1           | PC5  |
| TX_EN          | PB11 |
| TXD0           | PG13 |
| TXD1           | PG14 |

PHY address **0** (`LAN8720_PHY_ADDRESS`). The LAN8720A uses its internal
power-on reset; no MCU NRST pin is driven (PI1 is the MPU6050 INT pin on this
board, so the reference's PI1 PHY-reset mapping is intentionally not used -
the BCR software reset suffices).

## What happens

1. `main()` sets up the 180 MHz clock, UART, and `lwip_init()`.
2. `Netif_Config()` adds the ETH netif (DHCP by default).
3. `http_server_init()` binds a raw-API TCP listener on port 80.
4. The main loop:
   - `ethernetif_input()` polls received frames into lwIP,
   - `sys_check_timeouts()` runs the lwIP timers,
   - `Ethernet_Link_Periodic_Handle()` / `DHCP_Periodic_Handle()` drive the
     PHY link check and DHCP state machine.

The ETH DMA descriptors / RX buffers / TX bounce live in **internal SRAM**
(`.sram_dma`) - the F429 has no D-cache, so CPU<->DMA are coherent with no
cache maintenance.

## API (same as e_server / wifi_http_server)

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
because the board has no HTTP/TLS client.

## Web assets

The bundled site (`src/web_assets.h`) is generated from the shared
`e_server/web` + `e_server/public` sources by `e_server/build_web.py` at
build time (a CMake custom command). Edit the site in `e_server/web` and
rebuild.

## Sensors

The `/api/adc` endpoint reads the real on-board sensors (same drivers as
`app/board_hello`): ADC1 internal channels (VREFINT / die temperature / VBAT),
MPU6050 accelerometer (I2C1) and DHT11 (PE2).

## Console output

With the cable connected:

```
=== eth_http_server on fire-f429 (LAN8720A, RMII) ===
HTTP server: http://<dhcp-ip>/  (DHCP enabled)
ETH: LAN8720A PHY OK (ID 0007:c0f1)
ETH: looking for DHCP server ...
ETH: DHCP IP = 192.168.x.x
HTTP server listening on :80
```

The **HTTP listener only comes up after the IP is assigned** (DHCP bound or
the static fallback) - it is started by the DHCP state machine in
`src/app_ethernet.c`, not at boot. With the cable unplugged the board prints
only up to the PHY line and never starts the listener (no "listening" line).
If DHCP times out it falls back to the static IP in `src/main.h`
(192.168.5.200) and then starts the listener.

## Files

- `src/main.c` - NO_SYS main loop (RX poll + lwIP timers + link/DHCP)
- `src/ethernetif.c/h` - HAL ETH + lwIP netif driver (RMII, `.sram_dma` buffers)
- `src/lan8720a.c/h` - minimal LAN8720A PHY driver (MDIO, auto-neg, PHYSCSR)
- `src/app_ethernet.c/h` - netif config + DHCP state machine + link periodic;
  starts the HTTP listener once the IP is assigned
- `src/http_server.c/h` - raw-API HTTP server (`http_server_hw_init` at boot,
  `http_server_start` after DHCP)
- `src/lwipopts.h` - lwIP NO_SYS configuration
- `src/main.h` - MAC address + static fallback IP
- `src/arch/cc.h` - lwIP compiler/arch config (NO_SYS)
- `src/web_assets.h` - generated site bundle

## Build and flash

```bash
cmake -G Ninja -B build .
ninja -C build
ninja -C build flash
```

Connect the board to the LAN via its RJ45 and browse to `http://<dhcp-ip>/`.
