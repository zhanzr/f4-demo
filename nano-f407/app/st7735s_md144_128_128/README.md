# st7735s_md144_128_128 — nano-f407 (STM32F407VET6)

> **STATUS: WIP — NEEDS FIX**
>
> The panel is driven with **soft (bit-banged) SPI** and the proven
> nucleo-f042k6 `st7735_softSPI`-style driver, but it still shows **random /
> partial "Z-shape" corruption on power-up** and usually requires several
> hardware resets before patterns render correctly. The MCU never hangs (the
> console loop keeps running), so this looks like a **hardware / power /
> panel** issue rather than firmware. **Plan: re-test this same LCD module on a
> different board to isolate the hardware problem.** Not for general use until
> resolved.

ST7735S **1.44" 128x128** TFT demo for the **nano-f407** board
(STM32F407VET6 @ 24 MHz). The panel is driven over **soft (bit-banged) SPI**
after hardware SPI2 proved unreliable on this panel.

## Wiring

| LCD pin | nano-f407 pin | Notes                          |
| ------- | ------------- | ------------------------------ |
| LCD_SCL | **PB13**      | soft SCL (GPIO out)            |
| LCD_MOSI| **PB15**      | soft MOSI (GPIO out)           |
| LCD_CS  | **PB12**      | software CS                    |
| LCD_DC  | **PC6**       | register/data                  |
| LCD_RST | **PB1**       | GPIO reset (not tied to MCU RST) |
| LCD_BL  | **PE9**       | TIM1_CH1 PWM @ 8% (limits backlight current) |

## What it demonstrates

- **Soft (bit-banged) SPI** mode 0 driving an RGB565 ST7735S panel — full
  software control of MOSI/SCL, no hardware-SPI clock/phase/polarity
  marginality.
- A 128x128 framebuffer rendered with **RGB565** (16-bit) colors.
- Patterns cycled in a `while(1)` loop: **solid red / green / blue,
  8-color bars, circle, square, triangle**. Frame writes are chunked with
  re-asserted CS and an inter-write CS-deselect gap so a single corrupt bit
  cannot desync the whole frame.

## Driver notes

- Portrait orientation, MADCTL `0xC8`. The 128x128 active area is offset by
  **+2 columns and +3 rows** onto the ST7735S 132x162 GRAM.
- Init blends the proven nucleo-f042k6 `st7735_softSPI` values (INVOFF `0x20`,
  NORON `0x13`, CubeFW gamma tables) with this panel's MADCTL/offsets.
- MCU clock is **24 MHz** (PLLN=192/PLLP=8, VCO=192 MHz) and TIM1 backlight PWM
  is 8% — set low on purpose to reduce power draw / browning-out.
- `CMakeLists.txt` appends the TIM HAL source for the backlight PWM
  (`stm32f4xx_hal_tim.c`, `stm32f4xx_hal_tim_ex.c`); SPI is bit-banged so no
  HAL SPI.

## Build / flash

```bash
cd app/st7735s_md144_128_128 && bash build.sh   # or: mkdir build && cd build && cmake -G Ninja .. && ninja
ninja flash                                      # programs the board via probe-rs / ST-Link (SWD)
```

Console (pattern names) is USART1 PA9/PA10 at 115200 baud on the CH340 adapter
(currently COM55, was COM4).

## References

- Proven soft-SPI driver (tested on a different 0.96" 160x80 panel):
  `D:\nucleo-f042k6\stm32f042-demo` branch `st7735_softSPI`.
- Vendor MD144 init example:
  `D:\board_database\module-lcm-md144-128x128-st7735s\vendor_example\STM32F103R\hard_spi`.
