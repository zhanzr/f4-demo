# app/dhry_180m - Dhrystone 2.1 @ 180 MHz (SDRAM stage-2 app)

The **stage-2 SDRAM/NAND app** counterpart of `bare/dhry_180m`: the same
Dhrystone 2.1 sources and compiler flags, but linked entirely in SDRAM at
`0xC0000000` and booted by `tool/boot` from NAND. Unlike the fire-f429 app
(which keeps code in internal flash and only remaps data into SDRAM), this
apollo stage-2 app runs **both code and data from the external W9825G6KH
SDRAM**, so the benchmark also pays the SDRAM code-fetch latency.

Shared with the bare twin (single copy):

- `../../bare/dhry_180m/src/{main,utils,dhry_1,dhry_2}.c`

app-specific files: `src/system_app.c` (non-destructive `SystemInit` - the
bootloader owns the clock tree) + `app.ld` (SDRAM at `0xC0000000`, stack in
built-in SRAM).

## Results

Measured on hardware, 180 MHz, 2,000,000 runs (GCC default flags
`-Ofast -ffp-contract=fast -funroll-loops`, no LTO):

| Build | Dhrystones/s | DMIPS/MHz |
| ----- | ------------ | --------- |
| Bare, internal flash+SRAM | 386,175 | 1.221 |
| app, SDRAM (code + data) | **42,382** | **0.134** |

The SDRAM figure is ~9.1x lower than the bare one because the benchmark's
globals + heap (and this stage-2 app's code) all pay the external SDRAM
latency. All final-value checks passed (`Int_Glob=5`,
`Arr_2_Glob = Number_Of_Runs + 10`).

SDRAM takes ~50 s per 2,000,000-run pass, ~9x the bare runtime. The score is
independent of the run count, so for quick turnaround rebuild with
`-DDHRY_RUNS=200000` (`cmake -G Ninja -B build -DDHRY_RUNS=200000 .` before
`ninja`) - the number won't change, only the runtime.

> Do **not** use LTO for Dhrystone (see `bare/dhry_180m/LTO_on_dhrystone.md`);
> LTO hoists loop-invariant work out of the timed loop and inflates the score.

## Build and flash

```bash
bash build.sh            # == cmake -G Ninja .. && ninja
ninja flash              # -> NAND via the FMC-NAND algorithm, then reset
ninja flash-boot         # (helper) programs tool/boot into internal flash too
```

Prerequisite: `tool/boot` in internal flash (one-time per board). The two-stage
chain then boots Dhrystone from SDRAM and prints `Dhrystones per Second` on
the USART1 console (COM42, 115200).