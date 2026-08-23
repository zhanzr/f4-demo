# fire-f429 — STM32F429IGT6 development projects

Bare-metal projects for the **fire-f429** board (STM32F429IGT6, F42x/F43x
family), built with **CMake/Ninja** (Pico-style), debugged/flashed over
**SWD** with a **Keil ULINK2** (CMSIS-DAP), with `printf()` streamed out
**USART1 (PA9/PA10)** at 115200 baud via a USB-serial virtual COM port.

![fire-f429 board](board_images/board_0.jpg)
![fire-f429 board v1](board_images/board_v1.jpg)

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
- Default LED demos use **LED_1 (PD12)**. The RGB LEDs share pins with the DVI
  interface and are reserved for demos that explicitly need them.
- Console: **USART1** on **PA9 (TX) / PA10 (RX)**, AF7, **115200 8-N-1**
- Debug: **Keil ULINK2** (CMSIS-DAP, SWD) — probe selector `c251:2722:V0010M9E`

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
- `bare/dhry_180m` — Dhrystone 2.1, 2,000,000 runs (GCC **or** armclang).
  Measured GCC: **391,198 Dhrystones/s, 1.237 DMIPS/MHz**. See its README.
- `bare/coremark_180m` — CoreMark 1.0.1, 10,000 iterations (GCC **or**
  armclang). Measured GCC: **470.2 iterations/s**, crcfinal `0x988c`. See its
  README.
- `app/blink_hello` — the same blink + ADC demo with `.data`, `.bss`, and heap
  located in onboard SDRAM. `SystemInit()` initializes SDRAM through the HAL
  before the C runtime copies `.data` and clears `.bss`; the app prints linker
  addresses to verify the placement. Bare projects leave `DATA_IN_ExtSDRAM`
  undefined and keep their normal flash/SRAM layout.
- `app/dhry_180m` — Dhrystone with runtime data in SDRAM. Measured GCC:
  **175,700.609 Dhrystones/s, 0.556 DMIPS/MHz**.
- `app/coremark_180m` — CoreMark with runtime data in SDRAM. Measured GCC:
  **194.246 iterations/s**, crcfinal `0x988c`.

## Creating a project

Use the shared board layer in the project's `CMakeLists.txt`:

```cmake
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/stm32f429_board.cmake)
stm32f429_apply_board(${PROJECT_NAME}.elf "-O1")
```

For a bare-metal project, omit `DATA_IN_ExtSDRAM` and use the default linker
script `board/stm32f429igt6.ld`; `.data`, `.bss`, and heap stay in internal
SRAM. For an SDRAM-remapped project, set
`STM32_LINKER_SCRIPT` to `board/stm32f429_sdram.ld` and define
`DATA_IN_ExtSDRAM` on the target. The shared startup then initializes SDRAM
before copying `.data` or clearing `.bss`; the early HAL state stays in
internal RAM.

## CCM RAM (0x10000000)

The 64 KB **CCM** (Core Coupled Memory) is CPU-only (D-bus, 0 wait states —
not accessible by DMA/AHB masters). The linker script uses it for:

- the **.ccmram** section — variables placed there explicitly via
  `__attribute__((section(".ccmram")))`,
- the **stack** (`.stack`, NOLOAD) — CPU-only access, so SRAM1/2/3 stay fully
  available to the application and AHB masters.

The heap lives in normal SRAM (limit = top of RAM, `_eram`).

## Clock tree (180 MHz)

```
HSE 25 MHz → PLL (M=25, N=360, P=2, Q=7) → SYSCLK 180 MHz
  AHB=180, APB1=45, APB2=90, flash latency 6, VOS scale 1
```

## Build / flash / serial console

```bash
cd bare/blink_hello && bash build.sh        # or: mkdir build && cd build && cmake -G Ninja .. && ninja
ninja flash                                 # OpenOCD (default; ~3 s, verify+reset)
ninja flash-probe                           # probe-rs (~11 s)
ninja flash-pyocd                           # pyOCD (~9 s)
```

Flash methods (measured on this board, ULINK2, ~45 KB image, erased baseline):

| Method    | Command (ninja)  | Time   | Notes                         |
| --------- | ---------------- | ------ | ----------------------------- |
| OpenOCD   | `flash`          | ~3.3 s | default: fastest, verified+reset |
| pyOCD     | `flash-pyocd`    | ~9 s   | ~7 s overhead when identical  |
| probe-rs  | `flash-probe`    | ~11 s  | erase + verify + reset        |

Read the console on the USB-serial virtual COM port (COMxx): **115200 baud,
8-N-1**. Example with PowerShell:

```powershell
$sp = New-Object System.IO.Ports.SerialPort('COM36',115200,[System.IO.Ports.Parity]::None,8,[System.IO.Ports.StopBits]::One)
$sp.ReadTimeout = 15000; $sp.Open()
$sb = New-Object System.Text.StringBuilder
$deadline = [DateTime]::Now.AddSeconds(15)
while([DateTime]::Now -lt $deadline){ try { $b = $sp.ReadExisting(); if($b){ [void]$sb.Append($b) } else { Start-Sleep -Milliseconds 200 } } catch { break } }
$sp.Close(); $sb.ToString()
```