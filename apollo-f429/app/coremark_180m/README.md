# app/coremark_180m - CoreMark 1.0.1 @ 180 MHz (SDRAM stage-2 app)

The **stage-2 SDRAM/NAND app** counterpart of `bare/coremark_180m`: the same
CoreMark 1.0.1 sources and compiler flags, but linked entirely in SDRAM at
`0xC0000000` and booted by `tool/boot` from NAND. Unlike the fire-f429 app
(which keeps code in internal flash and only remaps data into SDRAM), this
apollo stage-2 app runs **both code and data from the external W9825G6KH
SDRAM**, so the benchmark also pays the SDRAM code-fetch latency.

Shared with the bare twin (single copy):

- `../../bare/coremark_180m/src/{main,utils,core_portme}.c`
- `../../bare/coremark_180m/coremark_1_0_1/*.c`

app-specific files: `src/system_app.c` (non-destructive `SystemInit` - the
bootloader owns the clock tree) + `app.ld` (SDRAM at `0xC0000000`, stack/
`_sram_dma` in built-in SRAM).

## Results

Measured on hardware, 180 MHz, 10,000 iterations, SysTick timing (GCC
default flags `-Ofast -ffp-contract=fast -funroll-all-loops`):

| Build | iterations/s |
| ----- | ------------ |
| Bare, internal flash+SRAM | **495.47** |
| app, SDRAM (code + data) | **46.22** |

The SDRAM figure is ~10.7x lower than the bare one because the benchmark
(hot loops, list/matrix `malloc` heap + globals) runs entirely from the
external SDRAM. The run printed `crcfinal 0x988c`, **identical to the bare
run's CRC**, confirming the workload executed correctly in SDRAM.

SDRAM is ~10x slower per iteration, so a full 10000-iteration run takes
~3.6 minutes on each boot. The score is independent of the iteration count,
so for quick turnaround rebuild with `-DCORE_ITERATIONS=2000`
(`cmake -G Ninja -B build -DCORE_ITERATIONS=2000 .` before `ninja`) - the
number won't change, only the runtime.

## Build and flash

```bash
bash build.sh            # == cmake -G Ninja .. && ninja
ninja flash              # -> NAND via the FMC-NAND algorithm, then reset
ninja flash-boot         # (helper) programs tool/boot into internal flash too
```

Prerequisite: `tool/boot` in internal flash (one-time per board). The two-stage
chain then boots CoreMark from SDRAM and prints `Iterations/Sec` on the USART1
console (COM42, 115200).