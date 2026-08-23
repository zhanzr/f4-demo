# LTDC 5-inch touch LCD test - fire-f429 (SDRAM app)

SDRAM-remapped counterpart of `bare/ltdc_touch_test`. Same demo logic, same
LTDC/touch drivers, same compiler flags; only the memory model differs.

Behavior:

- 8 colored squares bounce around the screen.
- A green theme identifies this build (the bare build is blue).
- Touch draws a white square with a green inner square; coordinates print on
  the serial console.
- FPS is measured over 2-second windows and printed on the console.
- A green status band at the bottom of the screen identifies the build.

Memory model: `SystemInit()` initializes SDRAM through the HAL before the C
runtime; `.data`, `.bss`, heap **and** the LTDC framebuffer all live in the
SDRAM `.sdram_fb` linker section (after `.data/.bss/heap`).

## Result (measured on hardware)

| Metric | Value |
| ------ | ----- |
| Framebuffer | `0xD0001008` (linker-placed after .data/.bss/heap) |
| FPS (800x480 RGB888, 8 shapes + band) | **37** |
| Fill rate | ~1,152,000 pixels/s |

## Result comparison with `bare/ltdc_touch_test`

Both builds run at the same **37 FPS** with the same workload: no significant
difference between the two project setups. The demo redraws the full screen
with a CPU fill loop; the bottleneck is the CPU fill loop itself, not where
in SDRAM the framebuffer lives. This contrasts with the CPU/SDRAM benchmarks
(CoreMark/Dhrystone), where SDRAM-resident application data measurably slows
compute because of read latency.

## Wiring

Identical to `bare/ltdc_touch_test`; see its README. Note in particular the
backlight (`LCD_BL = PD7`) and LCD enable (`DISP = PD4`) outputs, which must
both be driven HIGH or the panel stays dark.

## Build and flash

```bash
cmake -G Ninja -B build .
ninja -C build
ninja -C build flash
```