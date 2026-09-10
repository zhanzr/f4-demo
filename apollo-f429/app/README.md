# apollo-f429 / app — two-stage NAND-boot stage-2 projects

This folder holds the **stage-2 apps** of the apollo-f429 two-stage NAND boot.
Each project is a self-contained firmware linked to run entirely from **SDRAM
bank 1 at `0xC0000000`** (the NAND is not XIP-able on the F4 FMC, so the
bootloader copies the image from NAND to SDRAM and executes it there). The
stack is kept in built-in SRAM so call/ISR nesting never foots onto the
external bus.

Projects:

| Folder | What it is |
| ----------- | --------------------------------------------------------- |
| `blink_hello` | Migrated from `bare/blink_hello` (LED blink + ADC internal-channel report) + prints `&main`/`&bss` pointers proving the remap. |

Each stage-2 app uses the shared `NAND_APP` guards:
- `board.c` — `SystemClock_Config()` is a no-op (the bootloader owns clocks)
- `cmake/stm32f429_board.cmake` — `STM32F429_SYSTEM_SOURCE` override for the
  non-destructive `system_app.c`
- `cmake/nand-flash-targets.cmake` — `ninja flash` writes the app to NAND via
  the FMC-NAND probe-rs flash algorithm

## Two-stage boot

The stage-1 **bootloader** and the **NAND flash algorithm** are NOT apps; they
live in the sibling `tool/` folder (mirroring h723-mini's `tool/`):

- `tool/boot` — stage-1 bootloader, internal flash. Brings up 180 MHz /
  console / SDRAM / NAND, copies the app from NAND offset 0 to `0xC0000000`,
  validates, and jumps. Programmed **once** into internal flash.
- `tool/nand_flash_algo` — register-level position-independent probe-rs FMC-NAND
  flash algorithm that `ninja flash` uses to write the app `.hex` into the NAND.

## Build & flash

```bash
cd app/blink_hello && bash build.sh   # builds app.hex/.bin (SDRAM-linked)
ninja flash                           # writes app.hex -> NAND via the algo, resets
ninja flash-boot                      # (helper) programs tool/boot into internal flash too
```

Workflow:
1. `cd tool/boot && bash build.sh && ninja flash` — program the bootloader into
   internal flash (once per board).
2. `cd app/blink_hello && bash build.sh && ninja flash` — write the app to NAND;
   the board resets and the bootloader loads + runs it from SDRAM.

Console: USART1 PA9 (TX) / PA10 (RX) 115200 8-N-1 (COM42 VCP on this board).

## Gotcha: SDRAM is an XN region on the F4

On the Cortex-M4, `0xC0000000`-`0xDFFFFFFF` (FMC SDRAM) is External-device /
Execute-Never by default, so the app faults on the very first instruction fetch
unless the **bootloader's MPU region 0** marks it executable (Normal, cacheable,
executable). h723 doesn't hit this because its app runs from `0x90000000`
(External RAM, executable by default).

Bare-metal projects that use only built-in flash + SRAM live in the sibling
`bare/` folder instead.