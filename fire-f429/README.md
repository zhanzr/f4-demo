# fire-f429 — STM32F429IGT6 development projects

Bare-metal projects for the **fire-f429** board (STM32F429IGT6, F42x/F43x
family), built with **CMake/Ninja** (Pico-style), debugged/flashed through the
on-board **Fire CMSIS-DAP** over **SWD**, with `printf()` streamed out
**USART1 (PA9/PA10)** at 115200 baud — the CMSIS-DAP's **virtual COM port
(VCP)** is the console.

![fire-f429 board](board_images/board_0.jpg)

## Board facts

- MCU: **STM32F429IGT6** (Cortex-M4F @ up to **180 MHz**, FPU)
- Flash **1 MB** (`0x08000000`)
- SRAM **256 KB** = SRAM1 **112 K** + SRAM2 **16 K** + SRAM3 **64 K**
  (contiguous, `0x20000000`) + CCM **64 K** (`0x10000000`, not used by linker)
- HSE crystal **25 MHz**, LSE **32.768 kHz**
- LEDs (all **low-active**, LOW = ON):
  - **LED_R** — PH10
  - **LED_G** — PH11
  - **LED_B** — PH12
  - **LED_1** — PD12
- Console: **USART1** on **PA9 (TX) / PA10 (RX)**, AF7, **115200 8-N-1** → CMSIS-DAP VCP
- Debug: **Keil ULINK2** (CMSIS-DAP, `--probe c251:2722:V0010M9E`, SWD)
  - The on-board **Fire CMSIS-DAP** (`c251:f001`) does *not* deliver SWD responses
    on this Windows host (interrupt-IN pipe never returns data; only the VCP
    works), so the ULINK2 is used for flashing/reset instead.

> ⚠ **DVI interface / RGB LEDs:** the board also carries a DVI interface. When
> the **DVI interface is used**, `LED_R`/`LED_G`/`LED_B` (PH10/PH11/PH12) must
> be **disconnected from the MCU by the on-board jumpers** (they share the DVI
> data lines). For plain LED use, keep the jumpers fitted.

## Projects

| Folder      | What it is                                                |
| ----------- | --------------------------------------------------------- |
| `bare/`     | **Bare-metal** projects — use only the built-in flash + SRAM |
| `app/`      | Projects using **remapped external SDRAM**                |

- `bare/blink_hello` — LED blink + ADC internal-channel demo (LEDs blink one
  by one; prints the 180 MHz clock and VREFINT / temperature / VBAT over USART1).

## Clock tree (180 MHz)

```
HSE 25 MHz → PLL (M=25, N=360, P=2, Q=7) → SYSCLK 180 MHz
  AHB=180, APB1=45, APB2=90, flash latency 6, VOS scale 1
```

## Build / flash / serial console

```bash
cd bare/blink_hello && bash build.sh        # or: mkdir build && cd build && cmake -G Ninja .. && ninja
ninja flash                                 # probe-rs download + reset over Keil ULINK2 SWD
```

Read the console on the **CMSIS-DAP virtual COM port** (COMxx): **115200
baud, 8-N-1**. Example with PowerShell:

```powershell
$sp = New-Object System.IO.Ports.SerialPort('COM42',115200,[System.IO.Ports.Parity]::None,8,[System.IO.Ports.StopBits]::One)
$sp.ReadTimeout = 15000; $sp.Open()
$sb = New-Object System.Text.StringBuilder
$deadline = [DateTime]::Now.AddSeconds(15)
while([DateTime]::Now -lt $deadline){ try { $b = $sp.ReadExisting(); if($b){ [void]$sb.Append($b) } else { Start-Sleep -Milliseconds 200 } } catch { break } }
$sp.Close(); $sb.ToString()
```