# st7735_test — ST7735S 1.44" 128x128 LCD on nano-f411

Drives an ST7735S 1.44" 128x128 (**MD144**) module on the **nano-f411** board
(STM32F411CEU6 @ 100 MHz), ported from the vendor example
`C8T6_md144_t1` (bit-banged 4-wire SPI), with the backlight PWM'd at ~20 %.

## Wiring

Same as the vendor example, except **BL is PB9 with ~20% PWM** (vendor ties BL
to 3.3 V):

| LCD pin | MCU pin | Role |
| ------- | ------- | ---- |
| SCL | PA5 | SPI clock (bit-banged) |
| SDA | PA6 | SPI MOSI (bit-banged) |
| RES | PA7 | Reset |
| DC  | PA4 | Data/command |
| CS  | PB8 | Chip select |
| BL  | PB9 | Backlight — **TIM4_CH4, ~20% PWM** |

## What it does

Two demo sets loop forever:

1. **Vendor `TEST_STAND`** (500 ms between screens): window-border **frame**,
   **16-level gray** horizontal bars, **color bands**
   (red/red/green/green/blue/blue/white/white), then full **red, green, blue,
   white, black** fills. The init sequence (frame rate / power / gamma /
   MADCTL `0xC8`, 65k mode) matches the vendor `001_006_ST7735S_1.44_0xC8.h`
   verbatim.
2. **Animated patterns ported from the h723-mini `st7789` example**
   (adapted 240×240 → 128×128):
   - **floating & bouncing shapes** — 8 filled squares / circles / triangles
     bouncing off the walls (~5 s),
   - **pure colors** — RED..BLACK, ~2 s each,
   - **gradient** — animated HSV hue sweep across the full color wheel (~5 s),
   - **LED test** — board LED PC13 on/off,
   all with a **live FPS counter** drawn transparently in the bottom band.

The F411 runs at 100 MHz (vs the vendor's 72 MHz F103); the FPS counter shows
the real throughput of the bit-banged bus (~a few frames/s on the gradient).

## Backlight PWM

`src/backlight.c` sets up **TIM4_CH4 on PB9 (AF2)** at 1 kHz: `PSC=99`,
`ARR=999`, `CCR4=200` → **~20 % duty**. APB1 = 50 MHz, timer clock ×2 =
100 MHz. `Backlight_SetDuty()` rescales CCR for 0..100 %.

## Build / flash / console

```bash
cd app/st7735_test
bash build.sh          # == mkdir build && cd build && cmake -G Ninja .. && ninja
ninja flash            # probe-rs download + reset over ST-Link SWD
```

Console is the board's USART1 / ST-Link VCP (COM9, 115200 8-N-1); a banner
prints once at boot. The screens run continuously in `while(1)`.

## Files

- `src/main.c` — vendor-style TEST_STAND + st7789-style animated patterns (shapes/colors/gradient/LED) with an FPS counter
- `src/lcd.c` / `lcd.h` — driver + vendor demo screens + 24-bit color API, text, lines/rects/circles/fills and `LCD_CopyBuffer`
- `src/lcd/lcd_fonts.c` / `lcd_fonts.h` — ASCII 6x12 font (from the h723 st7789 example)
- `src/interface.c` / `interface.h` — bit-banged 4-wire SPI primitives (+ fast raster burst helpers)
- `src/blockwrite/blockwrite.h` — pixel-window helper
- `src/backlight.c` / `backlight.h` — PB9/TIM4_CH4 PWM