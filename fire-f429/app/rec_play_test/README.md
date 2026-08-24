# Record & playback test - fire-f429 (app)

Capsense-driven microphone **record & playback** with the on-board **WM8978**
audio codec. The recorded PCM stays **in RAM** (the 8 MB onboard SDRAM) - no
SD card, no FatFs, no WAV headers.

Ported from the Wildfire (野火) F429 example
`37-I2S_audio/I2S_record_play`, simplified: the SD-card/WAV plumbing is
replaced by a RAM buffer and a walking-chunk DMA engine.

> **Known limitation (hardware):** the single electret mic is AC-coupled into
> the shared LIP/RIP node and the capture intermittently hits the rails
> (clip). The digital post-processing (below) contains it, but occasional
> playback noise remains. Try a lower `WM8978_SetMicGain()` in
> `rec_play.c` if your unit is worse.

## Behavior

A simple 4-state machine. A **"press"** is a completed **down-then-up** pad
contact (the event fires on release); presses during recording/playing are
ignored — only the action completing (15 s recorded / samples played out)
advances the state. Every state change is printed on the console:

```
   WAIT_RECORD --press--> RECORDING --15 s done--> WAIT_PLAY --press--> PLAYING
        ^                                                                        |
        +---------------------------- play done --------------------------------+
```

1. **Power-on** → `WAIT_RECORD`.
2. **Press** → `RECORDING`: records **15 s** of MIC audio (44.1 kHz /
   16-bit / stereo, ~2.6 MB) into the SDRAM buffer, **PD12 LED ON**.
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
rec: start, 15000 ms (LED on)
post: peak=32768 -> gain x0.50
rec-diag: 1323000 samples (15.0 s)
rec-diag: L min=-32768 max= 32767 mean=   -11
rec-diag: R min=-32768 max= 32767 mean=   -11
rec-diag: head: 0 0 0 0 0 0 0 0 0 0 0 0
rec-diag: mid : 71 71 -56 -56 -56 -56 71 71 -56 -56 71 71
[state] RECORDING -> WAIT_PLAY
rec: done, 750 chunks (15000 ms)
[state] WAIT_PLAY -> PLAYING
play: start, 15000 ms (LED on)
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

- **Format**: 44100 Hz, 16-bit, stereo. 15 s = 1,323,000 u16 frames
  (~2.6 MB) in `pcm_buffer[]` (.bss → SDRAM via `stm32f429_sdram.ld`).
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
- **Codec paths** (vendor-matched values):
  - record: `MIC_LEFT|MIC_RIGHT|ADC_ON` (both input PGAs - the single mic
    drives the shared LIP/RIP node), outputs off (silent record),
    MIC gain 50, vendor R14 = HPF off / 128x OSR, 10 ms codec settle;
  - play: `DAC_ON` → earphone, volume 40.
- **Post-processing** (`RecPlay_PostProcess`, after each recording): mutes
  the first 400 ms (codec/HPF startup transient), removes the DC offset
  **per 20 ms chunk** (the AC-coupled mic front-end drifts), and
  peak-normalizes to 50% FS (gain clamped to x0.5..x64) for consistent
  playback loudness. Prints `post: peak=... -> gain x...`.
- **Diagnostics**: `wm8978: present` (I2C ACK probe at boot) and, after
  each recording, `rec-diag:` L/R min/max/mean plus head/mid sample dumps.
- **Engine hardening** (lessons from the debug sessions):
  - `Engine_Stop()` runs entirely on register writes (`HAL_DMA_Abort`'s
    `HAL_GetTick()` timeout can never expire inside a DMA ISR) **and**
    resets the HAL DMA handle states - otherwise the next session's
    `HAL_DMAEx_MultiBufferStart_IT()` returns `HAL_BUSY` and playback
    never starts;
  - the double-buffer `CT` bit is cleared before each session
    (`DMA_ForceM0First`) so every session deterministically starts on
    buffer M0;
  - buffer identity uses the HAL callback pair (`XferCpltCallback` = M0
    done, `XferM1CpltCallback` = M1 done), not a CT read inside the ISR;
  - DMA start results are checked and failures printed; a latched error
    code is exposed via `RecPlay_LastError()`;
  - main() runs a 15 s stall watchdog: if the engine stops progressing,
    the session is force-closed (LED off) instead of hanging forever.
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
