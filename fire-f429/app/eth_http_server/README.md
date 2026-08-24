# Ethernet HTTP server - fire-f429 (app, LAN8720A PHY via RMII)

A minimal HTTP server on the **fire-f429** board's on-board **LAN8720A**
Ethernet PHY (RMII), using **lwIP 2.2.1 (NO_SYS / raw API)** and the
**HAL ETH** driver. It serves the same **e_server** single-page site + JSON
API as `app/wifi_http_server`, but over wired Ethernet - no WiFi module.

Ported from the STM32F769I-Discovery `eth_http` project and the vendor F429
`42-ETH-LWIP` LAN8720A examples.

> **lwIP version**: the ETH server uses the plain **lwIP STABLE-2_2_1**
> vendored in `drivers/lwip` (a clean release, unified `include/` layout).
> The WiFi apps keep the **WICED fork (2.0.3)** under `drivers/wifi_ap6181`
> because the AP6181 SDK requires it - the two can coexist.

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
3. `http_server_start()` binds a raw-API TCP listener on port 80 (after DHCP).
4. The main loop:
   - `ethernetif_input()` polls received frames into lwIP,
   - `sys_check_timeouts()` runs the lwIP timers,
   - `http_stream_poll()` feeds the MJPEG stream,
   - `Ethernet_Link_Periodic_Handle()` / `DHCP_Periodic_Handle()` drive the
     PHY link check and DHCP state machine.

## TCP configuration notes (why the page used to hang)

- **Window scaling is required**: `tcpwnd_size_t` is `u16` unless
  `LWIP_WND_SCALE` is set, and `TCP_SND_BUF = 64 KB = 65536` wraps to **0**
  (the build emitted `-Woverflow`). `pcb->snd_buf` then starts at 0 and
  **every `tcp_write()` fails with `ERR_MEM`** - ICMP ping still works, but
  no TCP response is ever sent (browser hangs on `http://<ip>/`).
  `LWIP_WND_SCALE 1` (+ `TCP_RCV_SCALE 0`) makes the send buffer `u32`.
- `TCP_WND = 12*MSS` (was 2*MSS) keeps the sender pipelined.
- `MEM_SIZE = 80 KB` so a full QVGA JPEG frame (20-60 KB) can be copied
  into the lwIP heap by `/capture` and `/stream`.
- The old 500 ms **RMII watchdog** in `main.c` (re-selecting
  `SYSCFG->PMC` MII/RMII while the MAC was running) is removed - it glitched
  the 50 MHz RMII reference clock and dropped the PHY link during idle gaps
  (the LINK LED blinked). RMII is now re-latched only in the link-up path
  (`eth_rmii_relatch()`, MAC stopped).

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
GET  /api/camera       {"source":"ov5640","ready":1,"w":320,"h":240,"frames":N,"ts":..}
GET  /stream           live MJPEG (multipart/x-mixed-replace, QVGA 320x240)
GET  /capture          one JPEG frame (image/jpeg, Content-Length)
GET  /api/info         {"arch","lan_ip","public_ip":null,"geo":null,
                        "weather":null}
GET  /public/<name>    raw image bytes (from the embedded_files[] table)
```

The `public_ip` / `geo` / `weather` fields are `null` (the page shows "N/A")
because the board has no HTTP/TLS client.

## Camera (OV5640 MJPEG)

The on-board **OV5640** module streams JPEG over the web:

- **SCCB** control on I2C1 (PB6/PB7 - shared with the MPU6050/EEPROM, the
  driver probes the sensor ID 0x56 so the devices coexist).
- **DCMI** data bus: HSYNC PA4, PIXCLK PA6, VSYNC PI5, D0..D7 on
  PH9/PH10/PH11/PH12/PH14, PD3, PI6, PI7. PWDN on PG3, RST on PG2
  (the 挑战者 F429 core board wires RST to PG2 - the F429IG-V1V2 example's
  PB5 must not be used here).
- The sensor is configured for **QVGA (320x240) JPEG** output. The key
  registers: `0x3821` bit5 (COMPRESSION ENABLE / JPEG enable - this is the
  one that actually turns the JPEG encoder on), `0x4713` (JPEG mode select),
  `0x4300/0x501f` (YUV422 input to the encoder). The config is applied in
  three tables (vendor base + JPEG format + QVGA timing) with a retry loop
  that power-cycles the module until real JPEG frames flow (the module's
  24 MHz crystal start-up is occasionally slow).
- DCMI runs in **JPEG mode** with DMA2 Stream1 (circular) into a 128 KB
  internal-SRAM ring (`.sram_dma`). `OV5640_GetJpegFrame()` scans the ring
  for complete frames (`FF D8 FF ... FF D9`, minimum length) and returns the
  last one; `http_stream_poll()` (called from the main loop) pushes them to
  the active `/stream` client, and `/capture` serves one frame with
  `Content-Length`.
- Transient DCMI sync errors are cleared in the IRQ handler (the HAL would
  otherwise abort the DMA); a 2 s NDTR stall watchdog restarts the capture.

> **Known quirk (this module)**: the OV5640 JPEG encoder stalls a few
> seconds after configuration (it silently reverts to YUV output -
> `jfifo_ovf` stays 0, so it is not a FIFO overflow). The driver's
> `OV5640_HealthCheck()` (called from the main loop) detects the stall
> (no complete frame for 500 ms) and re-triggers the encoder via `0x3821`
> (COMPRESSION ENABLE) - a ~20 ms soft restart that reliably resumes the
> stream. The result is a live stream with brief pauses every few seconds;
> without this watchdog the stream would go blank permanently after boot.
> The web UI monitors `/api/camera`'s `frames` counter and reports/restarts
> if it stops advancing.

Boot console (camera):

```
OV5640: ready (QVGA 320x240 JPEG)
OV5640: selftest OK - N JPEG frames, last MB bytes, F fps
```

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
  `http_server_start` after DHCP; `/stream` + `/capture` MJPEG)
- `src/ov5640.c/h` - OV5640 driver (SCCB, JPEG QVGA config, DCMI+DMA ring,
  frame extraction, boot self-test)
- `src/lwipopts.h` - lwIP NO_SYS configuration (WND_SCALE, 64 KB snd buf)
- `src/main.h` - MAC address + static fallback IP
- `src/arch/cc.h` - lwIP compiler/arch config (NO_SYS)
- `src/web_assets.h` - generated site bundle
- `../../drivers/lwip` - vendored lwIP STABLE-2_2_1 (core + netif only)

## Build and flash

```bash
cmake -G Ninja -B build .
ninja -C build
ninja -C build flash
```

Connect the board to the LAN via its RJ45 and browse to `http://<dhcp-ip>/`.
