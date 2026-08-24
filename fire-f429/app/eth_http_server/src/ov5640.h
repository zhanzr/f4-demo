/**
  * @file    eth_http_server/src/ov5640.h
  * @brief   OV5640 camera driver for the fire-f429 board: DCMI + DMA capture
  *          of JPEG frames, served as an MJPEG stream.
  *
  * The OV5640 is configured for direct JPEG output (built-in JPEG encoder)
  * at QVGA (320x240). The DCMI peripheral + DMA2 continuously capture the
  * byte stream into an internal-SRAM circular buffer; a frame parser scans
  * for JPEG SOI (FFD8) / EOI (FFD9) markers to extract complete frames.
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

/* JPEG frame ring buffer (internal SRAM for DMA2). Size must be big enough
 * for a full QVGA JPEG frame (typically 20-60 KB; 128 KB is safe). */
#define OV5640_FRAME_BUF_SIZE   (128U * 1024U)

/* OV5640 SCCB address (7-bit 0x3C -> 8-bit write 0x78). */
#define OV5640_SCCB_ADDR        0x78U

/* Minimum plausible JPEG frame length (QVGA JPEG is > 1 KB); shorter
 * FF D8..FF D9 spans are random patterns in non-JPEG data. */
#define OV5640_MIN_JPEG_LEN     500U

/* Init the camera: power-on/reset, SCCB (I2C1), read ID, configure JPEG
 * QVGA, and start the DCMI+DMA continuous capture. Returns 0 on success. */
int  OV5640_Init(void);

/* Get the next complete JPEG frame (SOI..EOI) from the ring buffer.
 * Returns the length, or 0 if none available yet. The returned pointer is
 * valid until the next call. */
uint32_t OV5640_GetJpegFrame(const uint8_t **frame);

/* True once the camera is initialized and frames are flowing. */
int OV5640_Ready(void);

/* Boot-time diagnostic: waits ~2.5 s, reports DCMI frame events, JPEG
 * frames found, and ring content (prints to the debug UART). */
void OV5640_Selftest(void);

#endif /* __OV5640_H__ */