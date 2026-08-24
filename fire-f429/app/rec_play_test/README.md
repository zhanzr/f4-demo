# Record & playback test - fire-f429 (app)

Capsense-driven microphone **record & playback** with the on-board **WM8978**
audio codec. The recorded PCM stays **in RAM** (the 8 MB onboard SDRAM) - no
SD card, no FatFs, no WAV headers.

Ported from the Wildfire (野火) F429 example
`37-I2S_audio/I2S_record_play`, simplified.

## Behavior

A simple 4-state machine. A **"press"** is a completed **down-then-up** pad
contact (the event fires on release); presses during recording/playing are
ignored — only the action completing (30 s recorded / samples played out)
advances the state. Every state change is printed on the console:

```
   WAIT_RECORD --press--> RECORDING --30 s done--> WAIT_PLAY --press--> PLAYING
        ^                                                                        |
        +---------------------------- play done --------------------------------+
```

1. **Power-on** → `WAIT_RECORD`.
2. **Press** → `RECORDING`: records **30 s** of MIC audio (44.1 kHz /
   16-bit / stereo, ~5.3 MB) into the SDRAM buffer, **PD12 LED ON**.
   Recording is **silent** (no output monitoring) and the pad is ignored.
3. Recording completes → LED **OFF**, state `WAIT_PLAY` (no playback here,
   the board just waits).
4. **Press** → `PLAYING`: plays until the recorded samples run out, with the
   **PD12 LED ON**. The pad is ignored; only "samples ran out" returns to
   `WAIT_RECORD`.

If the pad is still held down when an action ends, that press is swallowed
(released without effect) so it cannot immediately re-trigger the next one.

Console trace of a full cycle:

```
[state] power-on -> WAIT_RECORD
[state] WAIT_RECORD -> RECORDING
rec: start, 30000 ms (LED on)
rec: done, 1500 chunks (30000 ms)
[state] RECORDING -> WAIT_PLAY
[state] WAIT_PLAY -> PLAYING
play: start, 30000 ms (LED on)
play: done
[state] PLAYING -> WAIT_RECORD
```

## Hardware

| Function          | Pin  | Note                                        |
| ----------------- | ---- | ------------------------------------------- |
| Capsense pad      | PA5  | TIM2_CH1 input capture (AF1)                |
| Record LED        | PD12 | LED_1, low-active                           |
| Codec control     | PB6/PB7 | I2C1, 400 kHz (WM8978 @ 7-bit 0x34)      |
| I2S2 WS (LRC)     | PB12 | AF5                                         |
| I2S2 BCLK         | PD3  | AF5                                         |
| I2S2 DACDAT       | PI3  | AF5                                         |
| I2S2 MCLK         | PC6  | AF5                                         |
| I2S2ext ADCDAT    | PC2  | AF6                                         |

DMA: **TX** = DMA1 Stream4 Channel0 (`SPI2_TX`), **RX** = DMA1 Stream3
Channel3 (`I2S2ext_RX`), both double-buffered.

## Implementation notes

- **Format**: 44100 Hz, 16-bit, stereo. 30 s = 2,646,000 u16 frames
  (~5.3 MB) in `pcm_buffer[]` (.bss → SDRAM via `stm32f429_sdram.ld`).
- **Engine**: I2S2 runs as the full-duplex master. Both DMA streams run as
  **20 ms chunks** (1764 frames) ping-ponging between two internal-SRAM
  buffers (`.sram_dma`; DMA1 cannot reach the external SDRAM). On every
  transfer-complete:
  - *record*: the just-filled chunk is copied into the next slot of
    `pcm_buffer[]`;
  - *play*: the next chunk of `pcm_buffer[]` is prefetched into the freed
    ping-pong buffer.
  One 7 KB copy every 20 ms is negligible for the 180 MHz core.
- **Full-duplex quirk**: `I2S2ext` only shifts while the I2S2 master
  transmits, so during recording the TX DMA streams a zero-filled dummy
  buffer (digital silence to the DAC) to keep the clocks running.
- **Clocking**: PLLI2S = HSE 25 MHz /M(25) ×N(271) /R(6) ≈ 45.17 MHz →
  MCLK = 44100 × 256 exactly.
- **Codec paths**:
  - record: `MIC_LEFT|MIC_RIGHT|ADC_ON`, outputs off (silent record),
    MIC gain 50;
  - play: `DAC_ON` → earphone, volume 40.
- **ISR-safety**: the engine stops itself from the DMA ISR using register
  writes only (`HAL_DMA_Abort` is avoided - it polls `HAL_GetTick()` for its
  timeout, which can never expire inside a DMA ISR since SysTick cannot
  preempt it).

## Build and flash

```bash
cmake -G Ninja -B build .
ninja -C build
ninja -C build flash
```

Serial console: USART1 115200 (COM36).
