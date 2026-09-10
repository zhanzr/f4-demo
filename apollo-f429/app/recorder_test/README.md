# app/recorder_test - record 10 s (WM8978 mic -> SDRAM), play it back

Stage-2 SDRAM app booted by `tool/boot`: uses the on-board **WM8978** audio
codec and **SAI1** to record 10 s of **44.1 kHz 16-bit stereo** PCM from the
microphone into SDRAM, then plays it back on the speaker — no SD card, no
WAV/FATFS (kept in the volatile SDRAM memory).

Ported from the vendored ALIENTEK Apollo **实验46 录音机实验** (wm8978 + sai),
simplified to an in-memory loop.

## Behavior

- **Recording** (10 s): WM8978 ADC on (MIC + LINE IN), speaker muted; SAI1
  Block A master TX drives the bit clock (zeros), Block B slave RX captures via
  DMA2 Stream5 double-buffer into **SDRAM**. **LED0 (PB1) ON**.
- **Playing**: WM8978 DAC on, speaker set; the captured SDRAM PCM is streamed
  back via SAI1 Block A **DMA2 Stream3** double-buffer. **LED0 OFF**.
- Loops forever: record 10 s → play -> record 10 s → ...
- LED1 (PB0) blinks during a phase; console (USART1) prints the phases.

## Hardware

- **WM8978** codec: control I2C = bit-banged PH4 (SCL) / PH5 (SDA), address
  `0x1A` (`myiic`, same pins as the board's AT24C02).
- **SAI1**: Block A = master TX, Block B = slave RX, clock from **PLLI2S**,
  pins **PE2/PE3/PE4/PE5/PE6 AF6** (FS/SCK/SD/MCLK).
- DMA: **DMA2 Stream3** (SAI-A TX), **DMA2 Stream5** (SAI-B RX), circular
  double-buffer. IRQ handlers `DMA2_Stream3/5_IRQHandler` are in `sai.c`.
- Sampling: `SAIA_SampleRate_Set(44100)` uses the vendored PLLI2SN/Q/DivQ
  table (HSE 25 MHz, PLLM 25).

## Build & flash

```bash
bash build.sh            # == cmake -G Ninja .. && ninja
ninja flash              # app -> NAND via the FMC-NAND algorithm, then reset
ninja flash-boot         # (helper) programs tool/boot into internal flash too
```

Prerequisite: `tool/boot` in internal flash (one-time per board).

## Structure

- `src/main.c` - record/play loop, WM8978 ADC/DAC routing, SAI start/stop,
  SDRAM PCM buffers, LED flags.
- `src/wm8978.c/.h` - WM8978 codec config (clean port of the vendored Apollo
  recorder example).
- `src/sai.c/.h` - SAI1 + DMA2 double-buffer driver (clean port; Block A TX /
  Block B RX, PLLI2S clock table, DMA IRQ callbacks).
- `src/myiic.c/.h` - bit-banged I2C on PH4/PH5 (WM8978 control).
- `src/sys_compat.h`, `src/sys.h`, `src/delay.c/.h` - ALIENTEK sys.h/delay.h
  shim onto this repo's HAL/board layer.
- `src/system_app.c`, `app.ld` - stage-2 SDRAM app glue (shared with the other
  `app/*` projects).

## Notes

- The PCF8574 is not involved in audio; the WM8978 + SAI path is
  self-contained.
- The board HAL conf enables `HAL_SAI_MODULE_ENABLED` + `HAL_RCC_EX_MODULE_ENABLED`
  (and `stm32f4xx_hal_sai_ex.c` is linked — the sync/clock helpers live there).
- Verified on target: records `1765376` bytes (10 s), plays back, loops.