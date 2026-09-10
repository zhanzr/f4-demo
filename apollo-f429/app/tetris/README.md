# app/tetris - Tetris (apollo-f429, stage-2 SDRAM app)

A Tetris game on the apollo-f429 (STM32F429IGT6 @ 180 MHz), booted as the
**stage-2 SDRAM/NAND app** (running at `0xC0000000`), drawing on the on-board
**2.8" ILI9341 240x320** TFT over the **16-bit FSMC parallel** bus.

Ported from `D:/c5_prj/nucleo-c542/board_t1/tetris` (ST7789 **240x240**
panel) - the game mechanic is unchanged, the layout is adapted to the taller
**240x320** panel and the input is rewired to this board's buttons.

## Controls

| Action          | Input                                                     |
| --------------- | --------------------------------------------------------- |
| start / pause / resume | **TPAD touch button** (TIM2-CH1 / PA5, 实验10)    |
| rotate          | **KEY_UP** (PA0, internal pull-down, active high)          |
| left            | **KEY_2**  (PC13, internal pull-up, active low)            |
| drop (hard)     | **KEY_1**  (PH2, internal pull-up, active low)             |
| right           | **KEY_0**  (PH3, internal pull-up, active low)             |

The four mechanical keys (实验2 按键输入实验 mapping) have **no external pull
resistors**, so the driver enables the MCU internal pulls (pull-down for
PA0, pull-up for the other three). Left/right auto-repeat (DAS: 220 ms,
ARR: 50 ms). Rotate uses ±1/±2 wall kicks.

## Game

- 10x20 playfield, 12 px cells, shifted down by 40 px to center on the
  240x320 panel; right panel with NEXT preview and SCORE / LINES / LEVEL.
- Landed blocks are repainted in a unified light gray; the falling piece
  keeps its tetromino color.
- Scoring: 100/300/500/800 x level for 1/2/3/4 lines; +2 per hard-dropped
  row. Level (and speed) up every 10 lines; gravity 800 ms down to 120 ms.
- Touch button toggles pause (PAUSE overlay); game-over overlay (blinking)
  offers START = replay; after 60 s idle on the game-over screen the title
  page returns.
- Title page shows the **top-5 high scores** (rank colors). Scores persist in
  the on-board **AT24C02 EEPROM** (I2C2, PH4/PH5, addr 0x50) via
  `board/ee_flash.c`, stored as `{magic, version, scores[5], crc}` with a
  **validity check** on load (bad/virgin/mismatched CRC -> fresh table).
- Random pieces use a 7-bag randomizer; the xorshift32 PRNG is seeded from
  the MCU UID + boot tick.

## Files

- `src/main.c` - game logic + input mapping + EEPROM top ranks
- `src/key.c/h` - 4 mechanical keys with internal pulls (实验2 mapping)
- `src/tpad.c/h` - capacitive touch button (实验10, TIM2-CH1/PA5)
- shared LCD/fonts/delay from `bare/lcd_touch_test/src` (single copy)
- `../../board/ee_flash.c` - AT24C02 I2C2 driver (HAL)

## Build and flash

```bash
bash build.sh            # == cmake -G Ninja .. && ninja
ninja flash              # -> NAND via the FMC-NAND algorithm, then reset
ninja flash-boot         # (helper) programs tool/boot into internal flash too
```

Prerequisite: `tool/boot` in internal flash (one-time per board). The two-stage
chain boots the game from SDRAM. Console (USART1 / COM42, 115200) logs new
games, pauses, line clears, level-ups and score-table updates.