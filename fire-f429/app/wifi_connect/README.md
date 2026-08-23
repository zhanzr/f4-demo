# WiFi connect - fire-f429 (app, EMW1062/AP6181 via SDIO)

Joins a Wi-Fi access point with the on-board **EMW1062** module (compatible
with AP6181/BCM43362) over SDIO1, obtains an IP address via DHCP, and prints
it over USART1 (115200). Uses the vendored WICED/WWD SDK in
`../../drivers/wifi_ap6181`.

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

Console output looks like:

```
==== fire-f429 WiFi connect (EMW1062 / AP6181 / SDIO) ====
Started FreeRTOS V9.0.0
Starting LwIP 2.0.3
Starting Wiced v006.002.001
Joining : scanned_ap_name_xx
Successfully joined : scanned_ap_name_xx
Obtaining IP address via DHCP
Network ready IP: 192.168.x.x
```

## Result

Verified on hardware: the module joins the configured AP and prints the DHCP
address over USART1, e.g. `Network ready IP: 192.168.5.93`.

Two reliability fixes were needed against a weak AP (-60 dBm, EAPOL M3
timeouts):

- **Power save disabled** (`wwd_wifi_disable_powersave()`): with power save
  the module sleeps and misses the broadcast DHCP OFFER/ACK frames, so DHCP
  never completes.
- **Patience + retries**:
  - `drivers/wifi_ap6181/WICED/WWD/internal/wwd_wifi.c`:
    `DEFAULT_JOIN_ATTEMPT_TIMEOUT` 7000 -> 15000 ms and
    `DEFAULT_EAPOL_KEY_PACKET_TIMEOUT` 2500 -> 8000 ms (some APs are slow to
    deliver M3 at edge of cell).
  - `src/connect.c` retries the join every 2 s (up to 60 tries); if DHCP does
    not bind within 45 s the association is dropped and the whole join + DHCP
    sequence restarts.

## Debugging note: the startup HardFault

The HAL starts the 1 kHz SysTick inside `HAL_Init()` — about 1 ms before the
FreeRTOS scheduler runs. If `SysTick_Handler` forwards those early ticks into
`xPortSysTickHandler()` while `pxCurrentTCB` is still NULL, the app HardFaults
deterministically ~1 ms after reset (in this project: exactly during the 11th
character of the startup banner). `src/it.c` gates the tick on a
`wiced_rtos_running` flag that `main()` sets right before
`vTaskStartScheduler()`, and still calls `HAL_IncTick()` so HAL timeouts keep
working.

## Build and flash

```bash
cmake -G Ninja -B build .
ninja -C build
ninja -C build flash
```

Requires an antenna connected to the module.
