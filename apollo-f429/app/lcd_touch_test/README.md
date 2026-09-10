# app/lcd_touch_test - stage-2 (SDRAM/NAND boot) LCD + touch app

The SDRAM-linked stage-2 twin of `bare/lcd_touch_test`: booted by
`tool/boot` into SDRAM at `0xC0000000` and driving the same **2.8" TFT
(ILI9341, 16-bit FSMC parallel)** and the full vendored ALIENTEK touch stack.

The driver sources are **shared** with `bare/lcd_touch_test/src` (single copy);
only `src/main.c` and `src/system_app.c` are app-specific:

- `src/main.c` - same vendor TFTLCD color loop + st7789_md169 patterns
  (info / TEST_STAND / gradient / LED / FPS) with **touch active on every
  page**.
- `src/system_app.c` - non-destructive `SystemInit` (the bootloader owns the
  clock tree), plus the `NAND_APP` guard in `board.c` that no-ops
  `SystemClock_Config`.
- `app.ld` - everything in SDRAM (`0xC0000000`); the stack stays in built-in
  SRAM.

## Build & flash

```bash
bash build.sh            # == cmake -G Ninja .. && ninja
ninja flash              # writes app.hex -> NAND via the FMC-NAND algorithm,
                         # then resets (tool/boot loads + runs it from SDRAM)
ninja flash-boot         # (helper) programs tool/boot into internal flash too
```

Prerequisite: `tool/boot` in internal flash (one-time per board). Then the
two-stage chain runs the LCD app from SDRAM.

## Fixes carried from the bare port

- LCD bus configured register-level (LL FMC) - the vendored `hal_sram.c` is
  unusable.
- 6x12 font decode fixed (row-major / LSB-first) so text is not garbled.
- `math_shim.c` provides `sqrt()` so the SDRAM link (`app.ld`, which does not
  discard libm) doesn't fail on newlib's ARM-state `sqrt`.
- Touch polled in all phases; `RST` corner clears.

The bare counterpart (`bare/lcd_touch_test`) runs the identical driver straight
from internal flash if you want to isolate the two-stage boot from the LCD code.
