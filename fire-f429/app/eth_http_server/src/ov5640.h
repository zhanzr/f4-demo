/**
  * @file    eth_http_server/src/ov5640.h
  * @brief   OV5640 camera driver for the fire-f429 board: DCMI + DMA capture
  *          of native JPEG frames, served as an MJPEG stream.
  *
  * The OV5640 runs in JPEG mode (the sensor's built-in encoder; the
  * "FD5640" module on this board was hardware-verified by app/jpeg_test to
  * emit valid JFIF JPEG when 0x3821 bit5 = COMPRESSION ENABLE). The DCMI
  * peripheral + DMA2 capture the byte stream into an internal-SRAM circular
  * ring (CONTINUOUS mode); a variable-size JPEG frame parser scans the ring
  * for complete SOI..EOI frames and hands them out. The HTTP layer serves
  * image/jpeg directly - no RGB565 -> BMP conversion, ~10x less bandwidth.
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

/* JPEG capture ring (internal SRAM, DMA2-acessible): sized so QVGA 320x240
 * JPEG frames (typically 8-20 KB, up to ~60 KB for very detailed scenes)
 * fit comfortably; the 192 KB SRAM easily holds this + the ETH DMA ring. */
#define OV5640_JPEG_RING_SIZE   (64u * 1024u)

/* Sensor geometry (JPEG frames are QVGA 320x240; the compressed size
 * varies per frame, see OV5640_GetFrame). */
#define OV5640_FRAME_W          320U
#define OV5640_FRAME_H          240U

/* OV5640 SCCB address (7-bit 0x3C -> 8-bit write 0x78). */
#define OV5640_SCCB_ADDR        0x78U

/* Init the camera: power-on/reset, SCCB (I2C1), read ID, configure JPEG
 * QVGA output, and start the CONTINUOUS DCMI + CIRCULAR DMA ring. Returns
 * 0 on success. */
int  OV5640_Init(void);

/* Get the latest complete JPEG frame (variable size) from the ring. Returns
 * 1 and sets *frame / *len when a complete SOI..EOI frame is available, or 0
 * if none yet. The returned pointer is valid until the next call. */
int OV5640_GetFrame(const uint8_t **frame, uint32_t *len);

/* True once the camera is initialized and frames are flowing. */
int OV5640_Ready(void);

/* Total frames handed out since boot (for status polling). */
uint32_t OV5640_FrameCount(void);

/* Health watchdog: if the sensor goes quiet (no DMA movement for a while),
 * power-cycle + reconfigure it. Call periodically from the main loop. */
void OV5640_HealthCheck(void);

/* Set JPEG image rotation: 0/90/180/270 degrees. This applies the OV5640
 * vflip (0x3820 bits 2:1) + hmirror (0x3821 bits 2:1) registers to rotate
 * the image in the sensor (no CPU-side transpose needed for 90/270 - the
 * sensor's native windowing flips the axes). Returns 0 on success. */
int OV5640_SetRotation(int deg);

/* Set the JPEG encoder quality: 0 (worst/smallest) .. 63 (best/largest).
 * Writes the OV5640 compression register 0x4407. Returns 0 on success. */
int OV5640_SetQuality(int q);

/* Boot-time diagnostic: waits ~1.5 s, reports how many JPEG frames were
 * captured (prints to the debug UART). */
void OV5640_Selftest(void);

#endif /* __OV5640_H__ */