# apollo-f429 / tool — stage-1 bootloader + NAND flash algorithm

Mirrors h723-mini's `tool/` folder: the machinery that backs the two-stage
NAND boot lives here, separate from the stage-2 apps in `app/` and the pure
bare-metal projects in `bare/`.

## Contents

| Folder | What it is |
| ----------- | --------------------------------------------------------- |
| `boot/` | **Stage-1 bootloader** (internal flash only): 180 MHz clocks / console / SDRAM / NAND bring-up, loads the app image from NAND offset 0 to SDRAM `0xC0000000`, validates + jumps. Fail = blinking LED1 + console message. |
| `nand_flash_algo/` | **probe-rs custom FMC-NAND flash algorithm** (register-level, position-independent) + `build_algo.py` + generated `target_nand_fmc.yaml`. Used by `app/*`'s `ninja flash` to write the app into the NAND. |

## Stage-1 bootloader (tool/boot)

A self-contained bare-metal project:

```bash
cd boot && bash build.sh   # == cmake -G Ninja .. && ninja
ninja flash                 # internal flash via OpenOCD (ULINK2, SWD)
ninja flash-probe           # internal flash via probe-rs
```

Programmed **once** into internal flash; afterwards the two-stage chain runs
from the NAND-resident app.

## NAND flash algorithm (tool/nand_flash_algo)

Builds the register-level FMC-NAND algorithm (MT29F4G08ABADA, 2048 B/page,
64 page/blk = 128 KiB sector, non-ECC main area) with `arm-none-eabi-gcc`,
extracts the position-independent blob, and writes `target_nand_fmc.yaml` for:

```bash
probe-rs download --chip-description-path target_nand_fmc.yaml \
    --chip STM32F429IG-NAND-nand_fmc --binary-format hex app.hex
```

Address `0xC0000000 + off` maps to NAND byte offset `off` (matching the stage-2
app's SDRAM link base). It is rebuilt automatically by `app/blink_hello`'s
`ninja flash` whenever `flash_nand_fmc.c` is newer than the YAML.

## Layout note

This mirrors the reference boards: `dev1-f407`/`nano-f407` keep projects in
`app/`, and tools/common machinery in their private `tool/`-style folders.
h723-mini keeps its bootloader + flash algorithm in `tool/h723_boot` and
`tool/qspi_map/algo` exactly like this.