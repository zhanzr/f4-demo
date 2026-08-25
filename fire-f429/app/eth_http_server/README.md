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
   - `http_stream_poll()` feeds the BMP camera stream,
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
GET  /api/camera       {"source":"ov5640-rgb565","ready":1,"w":160,"h":120,"frames":N,"ts":..}
GET  /stream           live stream (multipart/x-mixed-replace, 24-bit BMP parts)
GET  /capture          one frame (image/bmp, Content-Length)
GET  /api/info         {"arch","lan_ip","public_ip":null,"geo":null,
                        "weather":null}
GET  /public/<name>    raw image bytes (from the embedded_files[] table)
```

The `public_ip` / `geo` / `weather` fields are `null` (the page shows "N/A")
because the board has no HTTP/TLS client.

## Camera (OV5640 RGB565 / BMP)

The on-board **OV5640** streams live images over the web as **24-bit BMP**
(universally renderable in browsers; Chrome/Firefox do not render 16-bit
BMP, so the RGB565 frames are converted in software):

- **SCCB** control on I2C1 (PB6/PB7 - shared with the MPU6050/EEPROM, the
  driver probes the sensor ID 0x56 so the devices coexist).
- **DCMI** data bus: HSYNC PA4, PIXCLK PA6, VSYNC PI5, D0..D7 on
  PH9/PH10/PH11/PH12/PH14, PD3, PI6, PI7. PWDN on PG3, RST on PG2
  (the 挑战者 F429 core board wires RST to PG2 - the F429IG-V1V2 example's
  reset pin mapping must not be used here).
- The sensor runs **RGB565 at QVGA (320x240)** - the stable capture mode
  proven by `app/ov5640_to_lcd_clone` (the built-in JPEG encoder does not
  produce valid output through the F4 DCMI on this module). The stream is
  **4x sharper** than the old QQVGA 160x120.
- DCMI runs in **normal (non-JPEG) mode** with **SNAPSHOT capture**: DMA2
  Stream1 (normal mode) transfers ONE complete QVGA frame per snapshot into
  a 153600-byte internal-SRAM buffer (`.sram_dma`; the frame fits a single
  DMA buffer, avoiding the newer HAL's flaky >0xFFFF double-buffer path).
  `OV5640_GetFrame()` hands out the latest snapshot and re-arms the next
  capture (clean `HAL_DCMI_Stop` + fresh start - the resume-then-start
  window would let the DCMI overrun its FIFO and stall the re-arm).
  `http_stream_poll()` converts each frame to a 24-bit BMP (`frame_to_bmp()`,
  header + BGR pixels) and streams it to `/stream` in **chunks** (a QVGA BMP
  is 230 KB > the 64 KB `TCP_SND_BUF`, so the part is queued segment by
  segment paced by `tcp_sent`); `/capture` serves one BMP via `conn_send`.
- The HAL handles live in `.sram_dma` (internal SRAM) - this app's `.bss`
  sits in SDRAM, and the DCMI/DMA interrupt paths touch them from IRQ
  context. They are zeroed at init (`.sram_dma` is NOLOAD).
- A request-aware health watchdog re-inits the camera only while a client
  is actively polling for frames (an idle server never false-triggers).

Boot console (camera):

```
OV5640: ready (RGB565 320x240)
OV5640: selftest OK - N RGB565 frames (320x240), F fps
```

Measured on hardware: **~14 fps capture, ~8 fps QVGA BMP streamed** (the
stream rate is bounded by the 64 KB send buffer + ACK pacing).

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
  `http_server_start` on link-up; `/stream` + `/capture` BMP camera)
- `src/ov5640.c/h` - OV5640 driver (SCCB, RGB565 QQVGA config, DCMI+DMA ring,
  fixed-size frame extraction, DMA-based stall watchdog, boot self-test)
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
