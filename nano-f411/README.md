# nano-f411 — STM32F411CEU6 development projects

Bare-metal projects for the **nano-f411** board (STM32F411CEU6), built with
**CMake/Ninja** (Pico-style), debugged/flashed through an **ST-Link** over
**SWD**, with `printf()` streamed out **USART1 (PA9/PA10)** at 115200 baud —
the ST-Link's **virtual COM port (VCP)** is the console.

![nano-f411 board — top view](board_images/board_0.png)

> Note on SWV/ITM: the firmware enables DWT + ITM (for the DWT cycle counter),
> but the ST-Link VCP UART is the console; SWO capture is not used. F411 has no
> SWO-ready pin allocation used here (TRACESWO = PB3, AF0).

## Board facts

- MCU: **STM32F411CEU6** (Cortex-M4F @ up to 100 MHz, 512 KB flash, 128 KB SRAM,
  128 KB CCM — CCM is D-bus/data-only, see below)
- HSE crystal **25 MHz**
- LED: **PC13**, single, **low-active** (`LED_ON()` = GPIO_PIN_RESET)
- User button: **PA0**, momentary, **active-low** (`BTN_PRESSED()` = pin == 0)
- Console: **USART1** on **PA9 (TX) / PA10 (RX)**, AF7, **115200 8-N-1** → ST-Link VCP
- Debug: **ST-Link** (V2/V3) over SWD; probe-rs chip name `STM32F411CE`, auto-detected

Hardware photos: `board_images/` (`board_1.jpg`, `board_2.jpg`, `board_3.jpg`).

## Projects (`app/`)

| Project              | What it does |
| -------------------- | ------------ |
| `app/blink_hello`    | Blinks the PC13 LED + samples the **ADC1 internal channels** (temperature/VREFINT/VBAT on the shared IN18 input) and prints them |
| `app/dhry_100m`      | Dhrystone 2.1, 2,000,000 runs, GCC **or** armclang, `-Ofast -ffp-contract=fast -funroll-loops` |
| `app/coremark_100m`  | CoreMark 1.0.1, 10,000 iterations, GCC **or** armclang **or** starm-clang, `-Ofast`-class flags |
| `app/coremark_sram`  | CoreMark with the timed kernel linked into **SRAM** (0x20000000) and copy-in'd at startup; SysTick timing |

The two benchmarks build with either **arm-none-eabi-gcc**, **Keil Arm
Compiler 6 (armclang)** or — where supported — **ST Arm clang**, selected with
`-DSTM32_TOOLCHAIN=gcc|armclang|starm-clang` at configure time. All projects
share the board support in `board/` (100 MHz clock from the 25 MHz HSE, PC13
LED, PA0 button, USART1 console, newlib stubs, ST HAL wiring) and the CMake
helpers in `cmake/`.

> ⚠ **Do not use LTO for Dhrystone.** GCC `-flto` hoists loop-invariant work
> out of the timed region and inflates the score ~2.2× (still passing the
> checks). See `app/dhry_100m/LTO_on_dhrystone.md`.

## RAM / CCM code-execution

Shared `.ram_code` support lives in `board/` (startup copy-in loop + linker
`PROVIDE` defaults + `BOARD_LINKER_SCRIPT` in the CMake board helper).

- **SRAM is a single 128 KB block** at 0x20000000 — there is no SRAM1/SRAM2
  split on the F411 (unlike the F407). `coremark_sram` executes the kernel
  from its base.
- **CCM (128 KB @ 0x10000000) cannot run code on this part.** As on every
  STM32F4, the CCM RAM hangs off the Cortex-M4 **D-bus (data)** only, so
  instruction fetch from 0x10000000 bus-faults (verified on the nano-f407 /
  dev1-f407 — `BFSR.IBUSERR`, HFSR=0x40000000). CCM is **data-only**; code
  that must execute belongs in flash or SRAM. (`nano-f411` has no
  `ccm_probe` app because the failure is a chip-family design trait.)

## ADC internal channels (blink_hello)

The internal channels of the **ADC1** peripheral, which differ from the F407:

| Channel | What it is |
| ------- | ---------- |
| **ADC1_IN17** | VREFINT (internal reference, ~1.21 V) |
| **ADC1_IN18** | Temperature sensor **or** VBAT (shared input) |

> On F411 the temperature sensor and VBAT are **both** on IN18. The
> `TSVREFE` / `VBATE` bits in `ADC1->CCR` pick which path is connected (mutually
> exclusive), and the temp sensor also gates VREFINT — so VBAT gets a second,
> separate conversion pass. VBAT is measured through a **1/4 internal divider**
> on F411 (F407 used /2). VREFINT is used to back out the supply voltage, which
> then scales the temperature and VBAT readings.

Example output (once per second):

```
ADC1: temp=1000 code, VREFINT=1504 code, VBAT=790 code
     Vdda ~= 3296 mV, chip temp ~= 44 C, VBAT ~= 2542 mV
```

## Clock tree (100 MHz)

```
HSE 25 MHz → PLL (M=25, N=200, P=2, Q=4) → SYSCLK 100 MHz
  AHB=100, APB1=50, APB2=50, flash latency 3, scale 1
```

(PLLQ=4 gives 50 MHz for the PLL48CLK — USB needs 48 MHz, so USB is out of
spec here; none of these projects use USB.)

## Build / flash / serial console

```bash
cd app/blink_hello && bash build.sh        # or: mkdir build && cd build && cmake -G Ninja .. && ninja
ninja flash                                # probe-rs download + reset over ST-Link SWD (auto-detected)
```

Or flash with OpenOCD: `ninja flash-ocd`.

To pin a specific ST-Link, pass `-DDEBUG_PROBE=<selector>` at configure time
(see `probe-rs list` for the selector, e.g. `0483:374b:xxxx`).

Read the console on the **ST-Link virtual COM port** (the ST-Link VCP shows up
as a `COMxx`): **115200 baud, 8-N-1**. Example with PowerShell:

```powershell
$sp = New-Object System.IO.Ports.SerialPort('COMxx',115200,[System.IO.Ports.Parity]::None,8,[System.IO.Ports.StopBits]::One)
$sp.ReadTimeout = 15000; $sp.Open()
$sb = New-Object System.Text.StringBuilder
$deadline = [DateTime]::Now.AddSeconds(15)
while([DateTime]::Now -lt $deadline){ try { $b = $sp.ReadExisting(); if($b){ [void]$sb.Append($b) } else { Start-Sleep -Milliseconds 200 } } catch { break } }
$sp.Close(); $sb.ToString()
```