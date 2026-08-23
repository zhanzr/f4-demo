# WiFi AP scan - fire-f429 (app, EMW1062/AP6181 via SDIO)

Scans for available Wi-Fi access points with the on-board **EMW1062** module
(compatible with AP6181/BCM43362) and prints the SSID / BSSID / RSSI / channel
of every AP over USART1 (115200). Uses the vendored WICED/WWD SDK in
`../../drivers/wifi_ap6181`.

This project runs from flash/SRAM with `.data/.bss` in SDRAM (the standard app
memory model, `DATA_IN_ExtSDRAM`).

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
2. A FreeRTOS task starts LwIP, then `app_main()` (from the vendored
   `scan_app/scan.c`) powers on the 802.11 device and starts an active scan.
3. Each found AP is printed:

```
#001 SSID          : MyNetwork
     BSSID         : 00:1A:30:... 
     RSSI          : -45dBm
     Channel       : 6
```

## Result

Measured on hardware: the module enumerates on SDIO, `wwd_management_wifi_on`
succeeds, and the AP list prints to the serial console. Sample output (AP
names shown here are generic placeholders):

```
==== fire-f429 WiFi scan (EMW1062 / AP6181 / SDIO) ====
Started FreeRTOS V9.0.0
Starting LwIP 2.0.3
Starting Wiced v006.002.001
Starting Scan
Waiting for scan results...

#001 SSID          : scanned_ap_name_xx
     BSSID         : AA:BB:CC:DD:EE:FF
     RSSI          : -45dBm
     Max Data Rate : 300.0 Mbits/s
     Network Type  : Infrastructure
     Security      : WPA2 AES
     Radio Band    : 2.4GHz
     Channel       : 6

End of scan results - next scan in 5 seconds
```

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
