# jpeg_test — OV5640 ("FD5640") JPEG-output probe for fire-f429

Empirically tests whether the camera module can emit a valid JPEG stream on
the parallel DVP bus. Console-only (`USART1`/COM36, no display needed).

## Hardware-verified result (2026-08-26)

This specific module — silk-screened **"FD5640 500W-V1 1"** — is a **clone**
(MID `0x300C/D` = `0x2200`, genuine OmniVision reads `0x7FA2`) but its
**built-in JPEG encoder DOES work**:

```
PID = 0x5640
MID = 0x2200 (genuine OV5640 expects 0x7F 0xA2) => clone

DVP sanity: QVGA RGB565 + colorbar -> 153600 B full frame -> link LIVE

JPEG phase QVGA + JPEG enable:     SOI=1 EOI=2 DQT=2 SOS=1 APP0=1
  first SOI @ ...: ff d8 ff e0 00 10 4a 46 49 46 00 01 01 01 ... ff db 00 43
  -> valid JFIF APP0 + DQT header
JPEG phase mode 3:                 same clean JFIF header (fresh DMA advance)
JPEG phase esp32 list:             same clean JFIF header (fresh DMA advance)
```

The 3 phases advanced the DMA ring differently and found SOI at different
offsets after the ring was poisoned with `0xAA`, so the captures are fresh,
not stale.

## The key correction: variable-size JPEG needs CONTINUOUS + CIRCULAR DMA

Earlier attempts "proved" JPEG didn't work by using the **fixed-size snapshot
SNAPSHOT DMA** (which only works when the buffer exactly matches the frame
size, e.g. RGB565 QVGA = 153600 B = 38400 words). JPEG frames are
**variable-size**, so the snapshot path saw garbage / ~200 B per frame.

The working recipe (what this probe does):

```
HAL_DCMI_Stop(&DCMI_Handle);
DMA2_Stream1 circular ring (MemDataAlign HALFWORD, FIFO_FULL, MBURST_INC4);
HAL_DCMI_Start_DMA(&DCMI_Handle, DCMI_MODE_CONTINUOUS, jpeg_cap_buf, words);
  // wait ~1 s (ring keeps rotating)
HAL_DCMI_Stop(&DCMI_Handle);
scan the ring for FF D8 .. FF D9 (SOI..EOI) + FF DB/DQT + FF E0/APP0
```

JPEG enable register set (verified on this module):

```
0x3821 = 0x20   (bit5 = COMPRESSION ENABLE)   <- the crucial one
0x4713 = 0x02   (JPEG mode 2; 0x03 also works)
0x4300 = 0x00   (YUV422 output, JPEG routes through YUV)
0x501f = 0x30   (YUYV)
0x3002 = 0x00   (enable JPEG block; base table left 0x1c)
0x3006 = 0xff   (enable JPEG clocks; base table left 0xc3)
0x471c = 0x50   (JPEG mode / quant)
```

## Files

- `src/probe_jpeg.c` — the probe: ID/MID read, DVP colorbar sanity,
  per-phase JPEG marker scan + DMA NDTR proof + JFIF header hex dump.
- `src/bsp_ov5640.c/.h`, `src/bsp_i2c.c/.h` — copied from the proven
  `ov5640_to_lcd_clone` driver (SCCB + DCMI/DMA snapshot init).

## Build / flash / capture

```sh
cmake -G Ninja -B build .
ninja -C build              # -> jpeg_test.hex
ninja -C build flash        # OpenOCD via CMSIS-DAP (ULINK2) SWD
```

Capture the console:

```powershell
powershell -ExecutionPolicy Bypass -File d:\f4-demo\capture_boot.ps1
# then read d:\f4-demo\boot_log.txt
```