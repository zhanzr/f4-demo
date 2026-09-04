# dev1-f407 — STM32F407VET6 development projects

Bare-metal projects for the **dev1-f407** board (custom **STM32F407VET6**), built
with **CMake/Ninja** (Pico-style), debugged/flashed through a **Keil ULINK2**
(enumerates as a CMSIS-DAP probe) over **SWD**, with `printf()` streamed out
**USART3 (PD8/PD9)** at 115200 baud to the on-board RS232/RS485 transceivers.
Read it with any USB-serial adapter.

![dev1 board — top view](board_0.jpg)
![dev1 board — wiring](board_1.webp)

Schematic / board manual: `board_sch.pdf`.

> ## ⚠️ Board health / power safety notice (IMPORTANT — read first)
>
> **This specific board is damaged.** It was accidentally powered from too high a
> voltage (6 V), which blew several onboard components. It still runs all
> non-Ethernet projects correctly, but treat the board as partially damaged:
>
> 1. **Power supply range (safe).** This board needs **external power in the
>    range 4.5–5.5 V** to operate safely. Anything above ~5.5 V can damage
>    components — do **not** exceed it.
> 2. **J5 jumper / USB isolation.** When powering from external power, you must
>    **disconnect the USB cable from external power**, i.e. **remove the J5
>    jumper**, so the two supplies do not fight. (External supply 5 V + USB 5 V
>    back-feeding can push the rail high enough to stress parts.)
> 3. **Two CAN PHY chips (TJA1050) are almost certainly broken.** Smoke was seen
>    rising from these. Do not expect the CAN transceivers to work; the CAN
>    interface should be considered dead.
> 4. **The Ethernet PHY (DP83848) is broken — possibly also damaged by the same
>    over-voltage event.** (See the Ethernet note below.) Do not rely on the
>    Ethernet interface.
> 5. **The MCU runs much hotter than a normal F407 board.** The on-chip junction
>    temperature reads ~74–76 °C while idle/running (a healthy board reads
>    ~51 °C), yet all tests pass and behavior appears normal. The main chip is
>    **possibly partially damaged** — monitor temperature and don't push it hard
>    for long stretches.
>
> Verified working (after the damage) at 168 MHz on hardware: `blink_hello`,
> `blink_hello_48m`, `coremark_168m` (449.0 iter/s, crcfinal 0x988c), `dhry_168m`
> (355,114 Dhry/s), `eeprom_test` (I2C, PASS), `spi_flash_test` (SPI W25Q64,
> PASS). Only Ethernet is broken.

> Note on SWV/ITM: the firmware also enables DWT + ITM and the `swv` target is
> still defined, but the ULINK2's CMSIS-DAP firmware cannot capture SWO, so the
> working console is the UART.

## Board facts

- MCU: STM32F407VET6 (Cortex-M4F @ up to 168 MHz, 512 KB flash, 128 KB SRAM)
- HSE crystal **25 MHz**, LSE crystal 32.768 kHz (both fitted)
- LEDs: LED1-**PE13**, LED2-**PE14**, LED3-**PE15**, all **low-level ON**
- Buttons: BTN1-PE10, BTN2-PE11, BTN3-PE12
- On-board: DP83848 PHY, W25Q64 flash, 24C02 EEPROM, CAN/485/232 PHYs
- Console: **USART3** on **PD8 (TX) / PD9 (RX)**, AF7, **115200 8-N-1**
- Debug: Keil ULINK2 on the JTAG header, driven in **SWD** mode (TRACESWO is
  not used)

> ⚠️ **Ethernet (DP83848) is BROKEN on this board.** The PHY is detected on
> MDIO (addr 1, ID `2000:5c90`) and the link negotiates, but the RMII link is
> unstable — it flaps between 10M/100M, drops ~50% of frames, and cannot
> sustain a usable TCP connection (HTTP times out). Confirmed with both this
> repo's `app/eth_http_server` AND a known-good vendor reference project
> (`pav2000/stm32f407-dp83848`) using the same pins — both fail identically.
> This is a **hardware** issue (RMII 50 MHz clock / PHY strap / wiring), not
> software. Do not rely on the Ethernet interface.
>
> > This was observed **after** the over-voltage event that damaged the board (see
> > the board-health notice above) — the PHY may (also) have been damaged by the
> > too-high 6 V supply.

## Projects (`app/`)

| Project            | What it does |
| ------------------ | ------------ |
| `app/blink_hello`  | Cycles LED1/2/3 every 250 ms @ 168 MHz + UART banner/tick prints + internal ADC (VREFINT/temp/VBAT) sampling (GCC) |
| `app/dhry_168m`    | Dhrystone 2.1, 2,000,000 runs, GCC **or** armclang, `-Ofast -ffp-contract=fast -funroll-loops` |
| `app/coremark_168m`| CoreMark 1.0.1, 10,000 iterations, GCC **or** armclang **or** starm-clang, `-Ofast -ffp-contract=fast -funroll-all-loops` (AC6 `-Omax` reaches 541.15 it/s) |
| `app/coremark_sram` | CoreMark 1.0.1 from **SRAM1** — kernel in 0x20000000 (the 112 KB main SRAM; SRAM2's 16 KB is too small for `-Omax`), copy-in'd at boot (works: 330.72 iters/s SysTick) |
| `app/ram_test` | Minimal SRAM2/CCM execution probes (trivial function proves which regions run code) |
| `app/eth_http_server` | HTTP server over Ethernet (DP83848). ⚠️ **BROKEN** — see the Ethernet note above. |
| `app/eeprom_test`  | AT24C02 EEPROM (I2C1: PB8=SCL, PB9=SDA) erase/program/read test |
| `app/spi_flash_test` | W25Q64 SPI flash (SPI2: PE3=CS, PC2=MISO, PB10=SCK, PC3=MOSI) erase/program/read test |

### RAM / CCM code-execution test status

Measured with `app/ram_test` (and `coremark_sram`):

| Memory | Code execution | Note |
| ------ | -------------- | ---- |
| **SRAM1** (0x20000000) | **Works** | `coremark_sram` runs and validates with the kernel here (330.72 iters/s SysTick, crc 0x988c) — faster than SRAM2 for instruction fetch |
| **SRAM2** (0x2001C000) | **Works** | Code executes (verified by `ram_test`); 16 KB too small for the `-Omax` kernel, so the benchmark uses SRAM1 |
| **CCM** (0x10000000) | **Does not work** | Hard fault (BFSR.IBUSERR) on the first CCM instruction fetch |

**Root cause (confirmed):** CCM cannot execute code on these parts. `ccm_probe`
shows the CPU reads/writes CCM **data** fine and the correct
code is resident at the fetch address, but the **instruction fetch** from
0x10000000 always bus-faults. This matches the STM32F4 architecture: CCM RAM is
connected only to the Cortex-M4 **D-bus (data bus)**, so the instruction bus
never reaches it — CCM is a **data-only** region on these parts. Code must run
from flash or SRAM1/SRAM2 (system bus). It is a chip-level design trait,
**not** a board defect.

`blink` is built with arm-none-eabi-gcc. The two benchmarks build with any of
three toolchains — **arm-none-eabi-gcc**, **Keil Arm Compiler 6 (armclang)**,
or **ST's Arm Clang (starm-clang)** — selected with
`-DSTM32_TOOLCHAIN=gcc|armclang|starm-clang` at configure time for the C code
(GNU as for the startup file; GNU ld+newlib or LLD for linking) at the most
aggressive optimization level. `-DSTM32_LTO=ON` additionally enables LTO (gcc
via the GNU plugin, or starm-clang via LLD's native LLVM LTO).

All projects share the board support in `board/` (168 MHz clock from the 25 MHz
HSE, LED GPIO, UART printf, newlib stubs, ST HAL wiring) and the CMake
helpers in `cmake/`.

## Overall situation / workflow

**How to connect.** Power the board and plug in two USB things:

1. **ULINK2** (or any SWD/CMSIS-DAP probe) **or an ST-Link V2** on the debug
   header for flashing and reset — `probe-rs` sees the ULINK2 as `Keil ULINK2
   CMSIS-DAP` (`c251:2722:V0010M9E`) and the ST-Link V2 as `STLink V2-1`
   (`0483:3752:...`).
2. **USB-serial adapter** wired to the on-board RS232/485 transceiver for the
   console — **115200 baud, 8-N-1** (the adapter shows up as e.g. `COM19`).

No extra wiring needed on the board itself; everything lives on the board (see
the photos above and the schematic PDF).

> **Note on the ST-Link V2 "Target voltage (VAPP) is 0.02 V" warning.** When
> the board is powered from its own supply (not from the ST-Link's 3.3 V
> output), the ST-Link's VAPP sense pin reads ~0 V and probe-rs / OpenOCD /
> STM32CubeProgrammer all print a "target voltage too low" warning. This is
> **harmless and ignorable** — the board is powered, and all three tools
> connect, flash, and verify successfully despite the warning.

**How to choose a toolchain.** Two options, decided once:

- **Use GCC (recommended for everything going forward).** Open source,
  actively maintained, links with newlib out of the box, LTO works. CoreMark
  lands within ~5 % of armclang. This repo builds `blink` this way.
- **Use armclang (experimental, for benchmark curiosity only).** ~12 %
  faster on Dhrystone, ~5.5 % faster on CoreMark, but Arm Compiler for
  Embedded **6.24 is the final feature release** (defect fixes only), it is
  proprietary, and the C library integration fights GNU ld/newlib (see the
  shims below). Do **not** base new production code on it; if you want an
  actively-developed LLVM compiler, use the open **LLVM Embedded Toolchain**
  instead — it is a normal ELF/LLD toolchain with no ARMCLIB ABI surprises.

**How to flash / run.** In a project's build dir:

```bash
ninja flash          # probe-rs download + reset (auto-detects ULINK2 or ST-Link V2)
ninja flash-ocd      # alternative via OpenOCD (default cmsis-dap; use -DOPENOCD_INTERFACE=stlink for ST-Link V2)
ninja flash-cube     # alternative via STM32CubeProgrammer CLI (ST-Link V2)
```

The `flash` target auto-detects the connected probe. To force a specific one,
configure with `-DPROBE_SELECTOR=0483:3752:...` (ST-Link V2) or
`-DPROBE_SELECTOR=c251:2722:V0010M9E` (ULINK2).

**How to verify.** Open the serial console first, then flash (or `probe-rs
reset`) and watch the banner. `blink` prints periodic `blink: LED cycle N`
lines. The benchmarks self-validate: Dhrystone's final values must match its
expected globals, and CoreMark must print `Correct operation validated` with
crcfinal `0x988c`. A machine-verifiable CoreMark pass = the same CRC every
time plus an iteration count within the expected range.

## Toolchain / environment

- arm-none-eabi-gcc 15.3.1 (`D:/Arm/GNU Toolchain mingw-w64-x86_64-arm-none-eabi`)
- Keil Arm Compiler 6 / armclang 6.24.0 (`D:/Keil_v5/ARM/ARMCLANG/bin/armclang.exe`)
- STM32F4 HAL + CMSIS — **vendored** in the repo root `drivers/` (trimmed
  subset of STM32Cube_FW_F4 v1.28.3; see the root `README.md` for how to use
  the full package instead)
- probe-rs 0.32 (`cargo install probe-rs-tools`) — sees the ULINK2 as `Keil ULINK2 CMSIS-DAP` (`c251:2722:V0010M9E`) and the ST-Link V2 as `STLink V2-1` (`0483:3752:...`)
- OpenOCD 0.12 (`C:/msys64/mingw64/bin/openocd.exe`) — alternative flasher
- STM32CubeProgrammer CLI (`D:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe`) — alternative flasher
- CMake ≥ 3.13 + Ninja

## armclang hybrid toolchain (benchmarks)

`app/dhry_168m` and `app/coremark_168m` compile C with Keil's **armclang**
(LLVM 20 based), while the startup file is still assembled with GNU `as` and
everything is linked with **GNU ld + newlib** using the same linker script as
the GCC build. See `cmake/armclang-keil-toolchain.cmake`.

Flags: `-Ofast -ffp-contract=fast -funroll-loops` + `-mthumb -mfloat-abi=hard
-mfpu=fpv4-sp-d16`.

Three gotchas are handled in `cmake/`:

1. **`armclang_force_wint_t.h`** — armclang's bundled `stddef.h` shadows LLVM's
   and ignores the `__need_wint_t` protocol, breaking newlib's
   `sys/_types.h` (`unknown type name 'wint_t'`). A forced `-include` shim
   defines `wint_t` first.
2. **`printf_rename.h`** — armclang runs in ARMCLIB "standardlib" mode and
   specializes the *name* `printf` into the ARMCLIB ABI: a call to
   `__2printf` plus hidden references to `_printf_c`/`_printf_d`/... Those
   symbols only exist in ARMCLIB, so the GNU-ld link failed with
   `dangerous relocation: unsupported relocation (__2printf): Unknown
   destination type (ARM/Thumb)`. Renaming `printf` → `bench_printf` via a
   forced `-include` stops the specialization (a plain variadic call is
   emitted); `bench_printf` is a thin `vprintf` wrapper in
   `board/uart_printf.c` (armclang does not specialize `vprintf`), so all
   printf-family output still reaches the UART.
3. **GNU link rule** — CMake's ARMClang module would otherwise configure an
   `armlink`-style executable rule; `cmake/armclang-postproject.cmake`
   restores the GNU driver link after `project()`.

**`-Omax` works with `-fno-lto`.** Raw `-Omax` makes armclang emit every
object as `.llvm.lto` bitcode with no ELF symbols, which GNU ld cannot consume
(it links a broken binary with no code). Pairing `-Omax` with `-fno-lto`
(compile-time optimization only, normal ELF objects) makes GNU ld link it fine
and is the fastest CoreMark config on this board (541 it/s). Both are passed
as **C-only** flags via `-DBENCH_OPT_C="-Omax -fno-lto"` (with `BENCH_OPT`
cleared). armclang `-flto` in general is **not linkable with GNU ld** for the
same `.llvm.lto` reason. GCC `-flto` works fine (plugin resolves symbols), so
`STM32_LTO=ON` is GCC-only.

Results @ 168 MHz (same board, same 25 MHz HSE clock tree). Best per toolchain
— **no LTO** (LTO invalidates Dhrystone; see `app/dhry_168m/LTO_on_dhrystone.md`):

| Benchmark      | GCC 15.3.1            | armclang 6.24.0       | starm-clang 21.1.1    |
| -------------- | --------------------- | --------------------- | --------------------- |
| Dhrystone 2.1  | 355,114 Dhrystones/s  | 393,391 Dhrystones/s  | 398,963 Dhrystones/s  |
| Dhrystone      | 1.20 DMIPS/MHz        | 1.33 DMIPS/MHz        | 1.35 DMIPS/MHz        |
| CoreMark 1.0.1 | 448.5 iterations/s    | 541.2 iterations/s (`-Omax`) | 401.4 iterations/s    |

All runs validate (Dhrystone final values match; CoreMark `Correct operation
validated`, crcfinal `0x988c`).

Notable: applying the nano-f411 optimization campaign here (the `-Omax
-fno-lto` armclang recipe via `BENCH_OPT_C`) lifts armclang CoreMark from
451 to **541 it/s** — now the clear winner on CoreMark — while the ST/LLVM
line still leads Dhrystone (starm-clang 398,963 > armclang 393,391 > GCC
355,114). All CoreMark timings are SysTick-based (the 32-bit CYCCNT DWT
counter wraps at ~25.6 s and invalidates the longer SRAM runs — see
`app/coremark_sram/README.md`).

### Recommendation: do not adopt Arm Compiler 6

armclang is measurably faster here (≈12 % Dhrystone, ≈5.5 % CoreMark), but
this project still recommends **GCC for all production work**, because:

- **Arm Compiler for Embedded 6.24 is the final planned feature release.**
  Arm's release notes state future updates are limited to defect fixes with
  no additional features. It is a maintenance-only, proprietary toolchain.
- It is not a drop-in C library story: outside Keil's µVision/ARMCLIB
  ecosystem it needs the `wint_t` and `printf` shims above to link with
  newlib/GNU ld, and its `-Omax`/LTO are unusable in that setup.
- Arm's actively-developed LLVM compiler is the **open LLVM Embedded
  Toolchain** (normal ELF/LLD, no ARMCLIB ABI). If the armclang-style
  performance matters, re-run this benchmark campaign with that toolchain.

The benchmark numbers and caveats are documented per project in
`app/dhry_168m/README.md` and `app/coremark_168m/README.md`.

## Clock (all projects)

HSE 25 MHz → PLL (M=25, N=336, P=2, Q=7) → **SYSCLK 168 MHz**,
AHB=168, APB1=42, APB2=84, flash latency 5, regulator scale 1 (no overdrive).

## Build / flash / serial console

```bash
cd app/blink && bash build.sh        # or: mkdir build && cd build && cmake -G Ninja .. && ninja
ninja flash                          # probe-rs download, ULINK2 SWD (selected via --probe c251:2722:V0010M9E)
```

Or flash with OpenOCD: `ninja flash-ocd`.

Read the console with a USB-serial adapter wired to the RS232/485 transceiver:
**115200 baud, 8-N-1**. Example with the CMSIS-DAP_LU virtual COM port
(`VID_251&PID_F001`, seen as e.g. `COM19`):

```powershell
# PowerShell: dump 15 s of console output
$sp = New-Object System.IO.Ports.SerialPort('COM19',115200,[System.IO.Ports.Parity]::None,8,[System.IO.Ports.StopBits]::One)
$sp.ReadTimeout = 15000; $sp.Open()
$sb = New-Object System.Text.StringBuilder
$deadline = [DateTime]::Now.AddSeconds(15)
while([DateTime]::Now -lt $deadline){ try { $b = $sp.ReadExisting(); if($b){ [void]$sb.Append($b) } else { Start-Sleep -Milliseconds 200 } } catch { break } }
$sp.Close(); $sb.ToString()
```

Example blink console output:

```
blink: LED cycle 16 @ 16089 ms
blink: LED cycle 18 @ 18100 ms
```

> The ULINK2 probe must be selected explicitly (`--probe c251:2722:V0010M9E`)
> when another CMSIS-DAP device (e.g. the serial adapter) is also plugged in,
> otherwise probe-rs picks the first probe it finds.

## SWV / ITM printf (optional, needs an SWO-capable probe)

The firmware enables DWT + ITM (stimulus port 0) so `SWV_PutChar()` would work
with a probe that can capture SWO. `probe-rs itm swo` syntax (0.32) is:

```bash
probe-rs itm --probe <PROBE> --chip STM32F407VE --protocol swd --non-interactive swo 600000 168000000 2000000
```

TPIU clock = HCLK = **168 MHz**, SWO baud **2 Mbaud** (prescaler 84).
The ULINK2 in CMSIS-DAP mode reports "Requested SWO mode is not available on
this probe", so use an ST-Link/J-Link/DAPLink if you need SWV.
