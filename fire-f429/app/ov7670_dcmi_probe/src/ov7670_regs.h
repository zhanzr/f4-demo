#ifndef OV7670_REGS_H
#define OV7670_REGS_H

#include <stdint.h>

/* Write the ALIENTEK QVGA RGB565 table (the one proven to configure the
 * sensor; SCCB bit-bang). Returns 0 on success. */
uint8_t OV7670_ApplyConfig(void);

/* OpenMV/ESP32 RGB565 finishing touches after the base table:
 *   COM15 = 0xD0 (RGB565 + full range 00..FF; the table leaves 0x10 =
 *   limited range 10..F0 → dark/washed),
 *   then rewrite CLKRC (0x11) last (OpenMV: "must rewrite clkrc after
 *   setting the other parameters or the image looks poor"). */
void OV7670_OpenMvRgbTweak(void);

/* Force the sensor's test-pattern color bar ON (SCALING_XSC bit7 = 0 keep,
 * SCALING_YSC bit7 = 1 -> color bar enable). QVGA RGB565 color bars produce
 * 0xF800 / 0x07E0 / 0x001F (R/G/B) word patterns on the DVI bus. */
void OV7670_ColorBarOn(void);
void OV7670_ColorBarOff(void);

#endif /* OV7670_REGS_H */