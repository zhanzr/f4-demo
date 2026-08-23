# wifi_ap6181 - vendored AP6181 (EMW1062-compatible) WiFi SDK

Vendored, trimmed copy of the Embedfire AP6181 WiFi SDK for STM32F429.
The board's **EMW1062** WiFi module is compatible with the **Broadcom
AP6181/BCM43362**.

## Upstream

- Repository: `git@github.com:Embedfire-WiFi-AP/wifi-ap6181-f429-code.git`
- Commit: `99ace05` ("删除SDK过深目录的内容"), tag `v1.0`

## Why vendored (not a submodule)

The upstream repo is ~243 MiB of git history and ~1.16 GB working tree.
The trim policy in the request was: vendor if the SDK can be trimmed to
< 100 MiB, otherwise use a submodule.

This copy keeps only what the **scan** example needs and is **~36 MiB**,
so it is vendored. Trimming removed:

- 21 of 22 example projects (only `wifi_lwip_scan` sources are kept)
- `F429.lib` (24 MiB prebuilt library) - the scan example compiles the
  WWD/WICED stack from source, so the lib is not linked
- `WiFi_SDK/libraries` test/graphics/scripting/audio/filesystem bloat
- `WiFi_SDK/resources` apps/images
- `.git` history, Keil `Output/`/`DebugConfig/` build artifacts, the
  `F429_BCM43362_NoOS_NoNS_0527.zip` archive

## What is kept

- `WICED/` - WWD + WICED headers/sources, FreeRTOS 9.0.0, LwIP 2.0.3,
  STM32F4xx platform glue, BESL/security (for `wiced_crypto`)
- `include/`, `libraries/utilities`, `libraries/inputs`, `resources/firmware`
- `scan_app/` - the scan example's `User/` sources
- `LICENSE-Cypress.txt` - Cypress WICED license (from upstream `WiFi_SDK`)

## SDIO pin map (matches the fire-f429 board)

| Function | STM32 pin |
| -------- | --------- |
| SDIO_D0..D3 | PC8, PC9, PC10, PC11 |
| SDIO_CMD | PD2 |
| SDIO_CLK | PC12 |
| WLAN_WAKEUP_HOST (OOB IRQ) | PA0 |

Defined in `WICED/platform/MCU/STM32F4xx/WWD/platform.c`
(`wifi_sdio_pins[]`).

## Building

See `../../../app/wifi_scan/README.md`.
