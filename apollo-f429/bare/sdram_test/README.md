# SDRAM test - apollo-f429

Runtime FMC test for the onboard **W9825G6KH** SDRAM: **32 MiB**, 16-bit,
4 banks, **8192 rows (A0-A12)** / 512 columns, configured for a ~90 MHz FMC
clock from the board's 180 MHz HCLK (`SDClockPeriod_2`). The project uses the
normal flash/SRAM linker layout and accesses SDRAM directly at `0xD0000000`
(FMC bank 2); it does not remap code or data into external memory.

Compared with `../fire-f429`'s IS42S16400J (8 MiB, 4096 rows), this part is
**4× larger** and adds one row-address line (**A12**, FMC PG2), so the FMC is
configured as 13 row bits / 9 column bits and the self-refresh counter is
scaled for 8192 rows (683 vs 1386 at 90 MHz).

> The Apollo board places its SDRAM on **FMC bank 1** (base `0xC0000000`), with
> SDNWE/SDNE0/SDCKE0 on PC0/PC2/PC3 (not the fire-f429 board's bank-2
> PH6/PH7). Configuration follows the vendor example: CAS latency 3, read pipe
> delay 1, burst-1 sequential mode register `0x0230`, 8× auto-refresh on
> startup.

The test initializes the SDRAM, writes a deterministic 16-bit pattern across
the full 32 MiB, reads it back, checks every word, and reports DWT cycle
counts and calculated throughput over USART1 at 115200 baud. The idle loop
blinks **LED1 (PB0)**.

## Result

Measured on apollo-f429 hardware:

| Operation | Cycles | Throughput |
| --------- | ------ | ---------- |
| Write     | 117,460,087 | 49.04 MiB/s |
| Read      | 287,238,792 | 20.05 MiB/s |

Hardware result: `PASS (0 errors)`.

The read rate matches the fire-f429 board (20.05 vs 20.03 MiB/s) and the write
is within its range (49 vs 57 MiB/s) — the W9825G6KH is a standard 166 MHz-B
class part run at the same 90 MHz FMC SDCLK, so it is **not** slower than the
fire-f429 IS42S16400J. (An earlier print reported 14.0/5.0 MiB/s — that was a
32-bit overflow in the MiB/s formula for the 32 MiB size, fixed with 64-bit
arithmetic.)

## Build and flash

```bash
mkdir -p build && cd build
cmake -G Ninja ..
ninja
ninja flash          # OpenOCD (default), or ninja flash-probe
```

Connect to USART1 (PA9 TX / PA10 RX, 115200 8-N-1) and reset the board to
capture the result.