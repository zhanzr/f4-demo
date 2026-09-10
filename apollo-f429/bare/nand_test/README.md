# NAND FLASH test - apollo-f429

Runtime FMC test for the onboard **NAND flash** (Micron **MT29F16G08ABABA**
16 Gbit or **MT29F4G08ABADA** 4 Gbit — the board carries one of these),
ported from the ALIENTEK Apollo STM32F429 "实验40 NAND FLASH实验" example.

The NAND sits on **FMC NAND bank 3** (`0x80000000`, **NCE3 = PG9**), 8-bit
data bus (`PD0/1/4/5/11/12/14/15` + `PE7/8/9/10`), **R/B on PD6**. This
project uses the normal flash/SRAM linker layout — no remapping — the NAND is
only accessed at runtime.

## What it does

1. Initializes the FMC NAND controller (bank 3, MODE4, 8-bit) and reads the
   device ID.
2. Identifies the chip geometry (page size, pages/block, block count) from the
   ID.
3. In a loop (blinking **LED1 PB0** meanwhile):
   - **erase block 2**,
   - **write** a deterministic pattern to its first page (main area,
     non-ECC),
   - **read** it back and **verify every byte**,
   printing `erase/write/read` DWT cycle counts and `PASS`/`FAIL` over
   USART1 at 115200 baud.

## Measured (apollo-f429)

Chip detected: **MT29F4G08ABADA**, ID `0xDC909556`, geometry 2048 B/page,
64 page/blk, 4096 blocks (= 512 MB):

| Operation | Cycles | Throughput |
| --------- | ------ | ---------- |
| Erase     | 71,450  | — |
| Write     | 56,688  | 6,350 KB/s (2 KiB page) |
| Read      | 39,152  | 9,194 KB/s (2 KiB page) |

Hardware result: **PASS (0 errors)** on the erase → write → read-back verify
loop (block 2 / page 128).

## Hardware

| Signal | MCU pin |
| ------ | ------- |
| NCE3 (chip enable) | PG9 (AF12 FMC) |
| R/B (ready/busy) | PD6 (input) |
| Data D0-D7 | PD0, PD1, PD4, PD5, PD11, PD12, PD14, PD15 + PE7, PE8, PE9, PE10 |
| FMC window | 0x80000000 (bank 3), CLE=bit16, ALE=bit17 |

## Build and flash

```bash
mkdir -p build && cd build
cmake -G Ninja ..
ninja
ninja flash          # OpenOCD (default), or ninja flash-probe
```

Connect to USART1 (PA9 TX / PA10 RX, 115200 8-N-1) and reset the board to
capture the result.