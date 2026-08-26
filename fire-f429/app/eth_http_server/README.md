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
2. `Netif_Config()` adds the ETH netif with the **static IP 192.168.5.200**.
3. `http_server_start()` binds a raw-API TCP listener on port 80 (on link-up).
4. The main loop:
   - `ethernetif_input()` polls received frames into lwIP,
   - `sys_check_timeouts()` runs the lwIP timers,
   - `http_stream_poll()` feeds the JPEG camera stream,
   - `Ethernet_Link_Periodic_Handle()` drives the PHY link check.

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
GET  /api/camera       {"source":"ov5640-jpeg","ready":1,"w":320,"h":240,"frames":N,"ts":..}
GET  /stream           live stream (multipart/x-mixed-replace, image/jpeg parts)
GET  /capture          one frame (image/jpeg, Content-Length)
GET  /api/info         {"arch","lan_ip","public_ip":null,"geo":null,
                        "weather":null}
GET  /public/<name>    raw image bytes (from the embedded_files[] table)
```

The `public_ip` / `geo` / `weather` fields are `null` (the page shows "N/A")
because the board has no HTTP/TLS client.

## Camera (OV5640 native JPEG)

The on-board **OV5640** streams live **JPEG** images: the sensor's built-in
JPEG encoder is enabled and the raw `image/jpeg` bytes are served directly
(no RGB565 -> BMP conversion - ~10x less bandwidth than the previous 230 KB
BMP frames). This was hardware-verified by `app/jpeg_test` on this board's
"FD5640" module (valid JFIF: `FF D8 FF E0 'JFIF' ... FF DB ... FF DA ...
FF D9`).

- **SCCB** control on I2C1 (PB6/PB7 - shared with the MPU6050/EEPROM, the
  driver probes the sensor ID 0x56 so the devices coexist).
- **DCMI** data bus: HSYNC PA4, PIXCLK PA6, VSYNC PI5, D0..D7 on
  PH9/PH10/PH11/PH12/PH14, PD3, PI6, PI7. PWDN on PG3, RST on PG2
  (the 挑战者 F429 core board wires RST to PG2 - the F429IG-V1V2 example's
  reset pin mapping must not be used here).
- The sensor runs **JPEG at QVGA (320x240)**. JPEG output is enabled by the
  register list verified in `app/jpeg_test`: `0x3821 bit5` COMPRESSION
  ENABLE (the crucial bit), `0x4713=0x02` JPEG mode, `0x4300=0x00` YUV,
  `0x501f=0x30`, `0x3002=0x00`, `0x3006=0xff`, `0x471c=0x50`.
- JPEG frames are **variable-size**, so the DCMI runs **CONTINUOUS** with a
  **CIRCULAR DMA ring** (64 KB, `.sram_dma`, internal SRAM): the DMA keeps
  writing; `OV5640_GetFrame()` scans the ring for the **last complete
  SOI..EOI frame**, copies it to a linear staging buffer (`jpg_out` in SDRAM)
  and hands it to the HTTP layer. No re-arm is needed - the capture never
  stops.
- `http_stream_poll()` streams the JPEG (`image/jpeg` part) to `/stream` in
  chunks paced by `tcp_sent` (a QVGA JPEG is 8-20 KB, well under the 64 KB
  `TCP_SND_BUF`); `/capture` serves one JPEG via `conn_send`.
- The HAL handles live in `.sram_dma` (internal SRAM) - this app's `.bss`
  sits in SDRAM, and the DCMI/DMA interrupt paths touch them from IRQ
  context. They are zeroed at init (`.sram_dma` is NOLOAD).
- A request-aware health watchdog re-inits the camera only while a client
  is actively polling for frames (an idle server never false-triggers).

Boot console (camera):

```
OV5640: init ok (attempt 1, JPEG 320x240, first frame NNNN B)
OV5640: ready (QVGA 320x240 JPEG)
OV5640: selftest OK - N JPEG frames (320x240), F fps
```

Measured on hardware: **~14 fps capture, ~8 fps QVGA JPEG streamed at
8-20 KB/frame** (vs 230 KB/frame BMP - ~10-20x less bandwidth; the frame
rate is bounded by the ACK pacing, not the camera).

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
HTTP server: http://192.168.5.200/  (static IP)
ETH: LAN8720A PHY OK (ID 0007:c0f1)
ETH: link UP, static IP 192.168.5.200
HTTP server listening on :80
```

The board uses a **static IP (192.168.5.200)** - no DHCP. The HTTP listener
comes up when the link is up (`ethernet_link_status_updated` starts it). With
the cable unplugged the board prints only up to the PHY line and never starts
the listener (no "listening" line). Set your host adapter to the same subnet
(e.g. 192.168.5.240, as documented) and browse to `http://192.168.5.200/`.

## Files

- `src/main.c` - NO_SYS main loop (RX poll + lwIP timers + link/DHCP)
- `src/ethernetif.c/h` - HAL ETH + lwIP netif driver (RMII, `.sram_dma` buffers)
- `src/lan8720a.c/h` - minimal LAN8720A PHY driver (MDIO, auto-neg, PHYSCSR)
- `src/app_ethernet.c/h` - netif config + link periodic; starts the HTTP
  listener once the link is up (static IP)
- `src/http_server.c/h` - raw-API HTTP server (`http_server_hw_init` at boot,
  `http_server_start` on link-up; `/stream` + `/capture` JPEG camera)
- `src/ov5640.c/h` - OV5640 driver (SCCB, JPEG QVGA config, DCMI+DMA circular
  ring, JPEG SOI/EOI frame extraction, health watchdog, boot self-test)
- `src/lwipopts.h` - lwIP NO_SYS configuration (WND_SCALE, 64 KB snd buf)
- `src/main.h` - MAC address + static IP (192.168.5.200)
- `src/arch/cc.h` - lwIP compiler/arch config (NO_SYS)
- `src/web_assets.h` - generated site bundle
- `../../drivers/lwip` - vendored lwIP STABLE-2_2_1 (core + netif only)

## Build and flash

```bash
cmake -G Ninja -B build .
ninja -C build
ninja -C build flash
```

Connect the board to the LAN via its RJ45 and browse to
`http://192.168.5.200/`.
