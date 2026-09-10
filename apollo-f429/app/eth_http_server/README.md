# app/eth_http_server - Ethernet HTTP server (LAN8720A PHY, RMII)

A minimal HTTP server on the **apollo-f429** board's **LAN8720A** Ethernet
PHY (RMII), using **lwIP 2.2.1 (NO_SYS / raw API)** and the **HAL ETH**
driver. It serves the **e_server** single-page site + JSON API over wired
Ethernet. Runs as the two-stage **stage-2 SDRAM app** (booted by `tool/boot`
from NAND, executing at `0xC0000000`).

Ported from `fire-f429/app/eth_http_server` and the vendored Apollo
`实验58 网络通信实验`.

## Wiring (RMII, apollo-f429)

| Function     | Pin  |
| ------------ | ---- |
| RMII_REF_CLK | PA1  |
| MDIO         | PA2  |
| MDC          | PC1  |
| CRS_DV       | PA7  |
| RXD0         | PC4  |
| RXD1         | PC5  |
| TX_EN        | PB11 |
| TXD0         | PG13 |
| TXD1         | PG14 |

PHY address **0** (`LAN8720_PHY_ADDRESS`).

> **Pin sharing (vendor doc)**: ETH_MDIO shares PA2 with USART2_TX and
> RMII_TX_EN shares PB11 with USART3_RX, so those UARTs must stay disabled
> while Ethernet is in use. The board console here is USART1 (PA9/PA10), so
> there is no conflict.

> **ETH_RESET**: the LAN8720A reset pin is wired to **PCF8574 P7** (`ETH_RESET_IO`,
> inverted through Q1). `LAN8720_Init()` pulses it via the expander (100 ms
> asserted / 100 ms released) before touching MDIO. The PCF8574 bit-bang I2C
> runs on PH4 (SCL) / PH5 (SDA) via the shared `myiic` driver.

## What happens

1. `main()` boots (stage-2 from SDRAM, non-destructive `SystemInit`),
   `lwip_init()` + `Netif_Config()` bring up the ETH netif with the
   **static IP 192.168.5.200** (host PC adapter: 192.168.5.99).
2. `http_server_hw_init()` sets up the 2 LEDs (LED0/PB1, LED1/PB0) and the
   sensors (internal ADC + DHT11).
3. `http_server_start()` binds a raw-API TCP listener on port 80 once the
   link is up.
4. The main loop: `ethernetif_input()` polls RX, `sys_check_timeouts()`
   runs the lwIP timers, `Ethernet_Link_Periodic_Handle()` drives the PHY
   link check.

The ETH DMA descriptors / RX buffers / TX bounce live in **internal SRAM**
(`.sram_dma`) - the F429 has no D-cache and its ETH DMA cannot reach the
external SDRAM, so those are placed by `app.ld` at `0x20000000`. lwIP's pbuf
heap sits in the SDRAM `.bss` and TX frames are bounced through internal SRAM
as in the fire-f429 port.

## API (apollo e_server contract)

```
GET  /                 gzip page (Content-Encoding: gzip)
GET  /api/leds         {"leds":[0,0]}     (LED0/PB1, LED1/PB0 - low active)
POST /api/leds         body {"leds":[0,1]} -> applies to the two LEDs
GET  /api/adc          {"vrefint_mv":..,"temp_c":..,"vbat_v":..,
                        "dht11_t":..,"dht11_h":..,"ts":..}
GET  /api/info         {"arch","lan_ip","public_ip":null,"geo":null,
                        "weather":null}
GET  /public/<name>    raw image bytes (from the embedded_files[] table)
```

The `public_ip` / `geo` / `weather` fields are `null` (the page shows "N/A")
because the board has no HTTP/TLS client. The Camera tab was dropped from the
site for this board.

## Sensors

`/api/adc` reads the real on-board sensors (same drivers as
`bare/lcd_touch_test`): ADC1 internal channels (VREFINT / die temperature via
the factory two-point calibration / VBAT) and the DHT11 on PB12. Before each
DHT11 read the server releases the PCF8574 INT line that shares PB12
(`PCF8574_ReleaseINT()`), otherwise the DHT11 reads stuck-low.

## Console output

```
=== eth_http_server on apollo-f429 (LAN8720A, RMII) ===
HTTP server: http://192.168.5.200/  (static IP)
ETH: LAN8720A PHY OK (ID 0007:c0f1)
ETH: link UP, static IP 192.168.5.200
HTTP server listening on :80
```

Set your host adapter to 192.168.5.99 (same subnet) and browse to
`http://192.168.5.200/`.

## Files

- `src/main.c` - NO_SYS main loop (RX poll + lwIP timers + link checks)
- `src/ethernetif.c/h` - HAL ETH + lwIP netif driver (RMII, `.sram_dma`)
- `src/lan8720a.c/h` - LAN8720A PHY driver (PCF8574-P7 hardware reset, MDIO
  regs, PHYSCSR speed/duplex)
- `src/app_ethernet.c/h` - netif config + link periodic; starts the HTTP
  listener once the link is up (static IP)
- `src/http_server.c/h` - raw-API HTTP server (2 LEDs, ADC+DHT11 sensors)
- `src/lwipopts.h` - lwIP NO_SYS configuration (WND_SCALE, 64 KB snd buf)
- `src/main.h` - MAC address + static IP (192.168.5.200)
- `src/arch/cc.h` - lwIP compiler/arch config (NO_SYS)
- `src/web_assets.h` - generated site bundle (from `e_server/`)
- `../../drivers/lwip` - vendored lwIP STABLE-2_2_1 (core + netif only)
- shared drivers: `bare/lcd_touch_test/src/{adc_internal,dht11,pcf8574,
  delay}.c` + `vendor/eeprom/myiic.c`

## Build and flash

```bash
bash build.sh            # == cmake -G Ninja .. && ninja
ninja flash              # writes app.hex -> NAND via the FMC-NAND algorithm,
                         # then resets (tool/boot loads + runs it from SDRAM)
ninja flash-boot         # (helper) programs tool/boot into internal flash too
```

Prerequisite: `tool/boot` in internal flash (one-time per board). Then the
two-stage chain boots the HTTP server from SDRAM. Plug the board's RJ45 into
the host, set the adapter to 192.168.5.99, and browse to
http://192.168.5.200/.