# apollo-f429 `app` - two-stage NAND boot (bootloader + SDRAM app)

The on-board **MT29F4G08ABADA NAND** acts as the application's storage. Since
the F4 FMC NAND is **not memory-mappable** (no XIP, unlike the H7's OCTOSPI in
h723-mini), the stage-1 bootloader reads the app image from NAND and *copies*
it into **SDRAM** (W9825G6KH, bank 1 @ `0xC0000000`, 32 MiB), then jumps to
it. The app therefore runs entirely from SDRAM - both code and volatile
data/bss/stack. This is the F4 equivalent of h723-mini's `tool/qspi_map`
design (bootloader + flash algorithm + app linked at a remapped address), with
the NAND replacing the W25Q64 and SDRAM replacing DTCM/OSPI-XIP.

Distinction vs `fire-f429/app`: fire's `app/` uses SDRAM for `.data/.bss`
(DATA_IN_ExtSDRAM) but keeps code in internal flash - it does **not** remap a
code-space chip. Here the whole image lives on NAND and executes from SDRAM.

## Layout

```
app/
├── boot/   stage-1 bootloader, internal flash only (clocks/UART/LED/SDRAM/NAND,
│           loads image from NAND->SDRAM 0xC0000000, validates + jumps)
├── app/    stage-2 app, linked entirely at 0xC0000000 (SDRAM). blink_hello
│           migrated: prints &main + &app_bss_probe to prove the remap. Uses the
│           non-destructive SystemInit (the bootloader owns the clock tree).
└── algo/   probe-rs custom FMC-NAND flash algorithm (register-level,
            position-independent) + build_algo.py + generated target yaml.
```

## Build & flash

```bash
bash build.sh            # == cmake -G Ninja .. && ninja      (both boot + app)
ninja flash-boot         # probe-rs -> boot.hex into INTERNAL flash (ULINK2, SWD)
ninja flash-app          # (re)builds the NAND algo, writes app.hex to the NAND,
                         # then resets the board
```

Workflow:
1. `ninja flash-boot` (once per board; the bootloader then stays in internal flash)
2. `ninja flash-app` to update the stage-2 app in NAND; the board resets and the
   bootloader loads + runs it.

Console: USART1 PA9 (TX) / PA10 (RX) 115200 8-N-1 (COM42 VCP on this board).

## How the pieces fit

- `algo/`: `probe-rs download --chip-description-path target_nand_fmc.yaml
  --chip STM32F429IG-NAND-nand_fmc app.hex`. The algorithm treats address
  `0xC0000000 + off` as NAND byte offset `off`; 2048 B/page, 64 page/blk
  (128 KiB sector). Non-ECC (main area), same as `bare/nand_test`.
- `boot/`: after bring-up it reads NAND pages 0..63 (128 KiB) into `0xC0000000`
  (SP + thumb reset vector must end up inside the internal SRAM (stack) or the
  SDRAM window), sets `SCB->VTOR = 0xC0000000` and jumps. Fail = blinking LED1
  + console message.
- `app/`: `app.ld` puts every section in SDRAM (`LMA == VMA`, so the startup
  `.data` copy is a no-op; only `.bss` is cleared), with the **stack left in
  the built-in SRAM** (`0x20030000` top) so call/ISR nesting never foots onto
  the external bus. `system_app.c` mirrors h723-mini's `system_app.c`:
  `SystemInit()` leaves RCC alone but enables the FPU.

## Gotcha: SDRAM is an XN region on the F4

On the Cortex-M4, `0xC0000000`-`0xDFFFFFFF` (FMC SDRAM) is **External device /
Execute-Never** by default, so the app faults on the very first instruction
fetch unless the MPU marks it executable. h723 doesn't hit this because its
app runs from `0x90000000` (External RAM, executable by default). The bootloader
configures a **32 MiB MPU region 0 covering `0xC0000000` (Normal, cacheable,
executable)** before jumping — mirroring h723's `mpu_ospi_config()`.

## Repo conventions

Follows the repo rules: no code comments beyond what's needed, `board/` sits in
the board layer, verification is by building + flashing this real board.