# C542 st7789_md130_240x240 - possible HW-SPI issue (comparison with the working nano-f411 port)

Context: the same MD130 module (ST7789V, 1.3" 240x240) works on the
nano-f411 board with **both** driving methods (soft bit-bang and HW SPI1),
so the module itself is fine. On the C542 board
(`/d/c5_prj/nucleo-c542/board_t1/st7789_md130_240x240`) the hardware-SPI
phase showed a dark screen, and per that project's README a rollback test
failed on the HW bus at **72, 36 and 0.56 MHz** while the soft bus kept
working. The C542 board is currently unavailable; this note records the
software comparison and the most likely cause, to act on when the board is
back.

The key observation: a failure at **every** tested speed (including
0.56 MHz) is **speed-independent** - it points at frame-level signaling or
pin muxing, not signal integrity.

## HW SPI configuration comparison

| Item | C542 (dark on HW) | nano-f411 (works) |
| ---- | ----------------- | ----------------- |
| SPI mode | **mode 0** - `HAL_SPI_CLOCK_POLARITY_LOW` + `HAL_SPI_CLOCK_PHASE_1_EDGE` (SCK idles **low**) | **mode 3** - `SPI_POLARITY_HIGH` + `SPI_PHASE_2EDGE` (SCK idles **high**) |
| HW clock | 144 MHz / 2 = **72 MHz** (`ST7789_SPI1_PRESC = PRESCALER_2`) | 100 MHz / 4 = **25 MHz** |
| Direction | SIMPLEX_TX (1LINE) | SIMPLEX_TX (1LINE) |
| Data / bit order | 8-bit, MSB first | 8-bit, MSB first |
| NSS | internal (soft CS) | internal (soft CS) |
| Kernel clock | PCLK2 | PCLK2 (APB2) |
| Pins | PB3 = SCK, PB5 = MOSI (AF5) | PA5 = SCK, PA7 = MOSI (AF5) |
| DC/CS framing, 512-byte burst buffering, banner -> `LCD_UseHwBus()` -> `LCD_Reinit()` -> patterns flow | identical | identical |

The soft path on BOTH boards produces **mode-3-equivalent timing**: SCL
idles high (pin init state SET), each bit is set while SCL is high, then
SCL falls and rises - the panel latches on the rising edge. The module
works with that timing everywhere.

## Findings

1. **SPI mode is the only byte-protocol difference between the working
   nano HW path and the failing C542 HW path.** The nano uses mode 3
   (idle-high clock, a byte-for-byte replica of the proven bit-bang
   timing); the C542 generated config uses mode 0 (idle-low clock,
   `board_t1_cmake/generated/hal/mx_spi1.c`,
   `clock_polarity = HAL_SPI_CLOCK_POLARITY_LOW`,
   `clock_phase = HAL_SPI_CLOCK_PHASE_1_EDGE`). A mode-0 vs mode-3
   mismatch breaks the panel's serial framing **at any clock speed**,
   which matches the 72/36/0.56 MHz rollback result exactly. The
   ST7789V 4-wire serial interface expects SCL to idle high; with SCL
   idling low inside a CS frame (mode 0), some ST7789 gate-driver/IPS
   variants mis-frame the first bits and render nothing.
2. **The 72 MHz clock is a real but secondary concern.** It exceeds the
   ST7789V write-cycle spec (~15 MHz), but the 0.56 MHz rollback failure
   shows speed alone was not the root cause. After fixing the mode,
   re-test at a spec-friendly rate first (e.g. prescaler /8 = 9 MHz)
   before pushing back to 72 MHz.
3. **Pin muxing (secondary, board-specific):** PB3 is SWO/TRACED0 on the
   C542 (the board README notes PB3 was previously SWO). The soft path
   drives the same pin as plain GPIO and works, so the board trace to the
   module is electrically fine; but if the C542's AF5 mapping for PB3/PB5
   differs from what the generated `mx_spi1.c` assumes, the HW clock/data
   would never reach the pins - dark at any speed. Verify PB3 = SPI1_SCK
   and PB5 = SPI1_MOSI at AF5 in the STM32C542 datasheet.
4. **Flow differences: none.** Banner ordering, bus switching
   (`LCD_UseSoftBus()`/`LCD_UseHwBus()` re-mux + `LCD_Reinit()`), 512-byte
   TX burst buffering, DC/CS framing per command/transfer and the
   SIMPLEX_TX/NSS-internal settings are the same as the working nano port.

## Suggested experiments (when the C542 board is available)

1. **Switch the C542 SPI1 to mode 3** (one-line change):
   - in `board_t1_cmake/generated/hal/mx_spi1.c`:
     `clock_polarity = HAL_SPI_CLOCK_POLARITY_HIGH`,
     `clock_phase = HAL_SPI_CLOCK_PHASE_2_EDGE`, and
   - keep `SPI_HW_SetSpeed()` as-is (or start at prescaler /8 = 9 MHz).
   This is the single most likely fix; the nano port is effectively the
   proof (mode 3 works on this exact module family).
2. If still dark, **scope PB3/PB5 during the HW phase**:
   - clock toggling present but wrong framing -> mode issue persists
     (unlikely after step 1);
   - no toggling at all -> pin mux/AF issue: check the C542 AF table for
     PB3/PB5 and the `.ioc2` SPI1 config.
3. After the display is stable, re-test speed upward (9 -> 18 -> 36 ->
   72 MHz) to find the module's actual limit on that board's wiring.

## Where the change belongs

`board_t1_cmake/generated/hal/mx_spi1.c` (mode fields) - plus, if the
generated file is regenerated from the `.ioc2`, the SPI1 polarity/phase
settings in the CubeMX project (`board_t1.ioc2`). Optionally adjust
`bsp/st7789/interface.c` `ST7789_SPI1_PRESC` (currently
`HAL_SPI_BAUD_RATE_PRESCALER_2` = 72 MHz) to a lower rate for the first
retest.
