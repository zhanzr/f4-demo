/**
  * @file    eth_http_server/src/ov5640.h
  * @brief   OV5640 camera driver for the fire-f429 board: DCMI + DMA capture
  *          of raw RGB565 frames, served as a 24-bit BMP stream.
  *
  * The OV5640 runs in RGB565 mode (the vendor-proven stable mode - the
  * built-in JPEG encoder on this module does not produce valid output
  * through the F4 DCMI). Frames are QQVGA (160x120, 38400 bytes each).
  * The DCMI peripheral + DMA2 continuously capture the byte stream into an
  * internal-SRAM circular buffer (exactly 3 frame slots); a fixed-size
  * frame parser hands out complete frames. The HTTP layer converts
  * RGB565 -> RGB888 and serves 24-bit BMP (universally renderable).
  *
  * Wiring (fire-f429, same as the vendor 45-OV5640 example):
  *   DCMI_VSYNC -> PI5, DCMI_HSYNC -> PA4, DCMI_PIXCLK -> PA6
  *   DCMI_D0..D3 -> PH9..PH12, DCMI_D4 -> PH14, DCMI_D5 -> PD3,
  *   DCMI_D6 -> PI6, DCMI_D7 -> PI7
 *   PWDN -> PG3 (low = power on), RST -> PG2
  *   SCCB/I2C: SCL PB6, SDA PB7 (I2C1, same bus as the WM8978/MPU6050)
  */
#ifndef __OV5640_H__
#define __OV5640_H__

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stddef.h>

/* RGB565 frame geometry (QQVGA - the vendor's stable 15 fps mode). */
#define OV5640_FRAME_W       160U
#define OV5640_FRAME_H       120U
#define OV5640_FRAME_BYTES   (OV5640_FRAME_W * OV5640_FRAME_H * 2U)  /* 38400 */

/* Ring buffer (internal SRAM for DMA2) = exactly 3 frame slots so frames
 * are always aligned (never straddle the ring end). */
#define OV5640_FRAME_BUF_SIZE  (3U * OV5640_FRAME_BYTES)   /* 115200 */

/* OV5640 SCCB address (7-bit 0x3C -> 8-bit write 0x78). */
#define OV5640_SCCB_ADDR        0x78U

/* Init the camera: power-on/reset, SCCB (I2C1), read ID, configure RGB565
 * QQVGA, and start the DCMI+DMA continuous capture. Returns 0 on success. */
int  OV5640_Init(void);

/* Get the next complete RGB565 frame from the ring buffer (fixed size
 * OV5640_FRAME_BYTES). Returns 1 and sets *frame, or 0 if none available
 * yet. The returned pointer is valid until the next call. */
int OV5640_GetFrame(const uint8_t **frame);

/* True once the camera is initialized and frames are flowing. */
int OV5640_Ready(void);

/* Total frames found since boot (for status polling). */
uint32_t OV5640_FrameCount(void);

/* Health watchdog: if the sensor goes quiet (no frame for a while),
 * power-cycle + reconfigure it. Call periodically from the main loop. */
void OV5640_HealthCheck(void);

/* Boot-time diagnostic: waits ~2.5 s, reports DCMI frame events, JPEG
 * frames found, and ring content (prints to the debug UART). */
void OV5640_Selftest(void);

#endif /* __OV5640_H__ */