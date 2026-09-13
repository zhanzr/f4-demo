/*
  touch.h - CHSC6417 capacitive touch controller (I2C, bit-banged) for
  the jd9858_md130_360x360_chsc6417 project.

  Wiring (vendor TK013F1327 example): TOUCH_SCL = PB13 (push-pull host
  output), TOUCH_SDA = PB15 (open-drain, released to the slave for
  reads).

  Protocol (vendored example, ported): write register pointer 0x00,
  STOP, restart and read 3+ bytes. Vendor coordinate math:
    X = ((buf[0] & 0x40) >> 6) << 8 | buf[1]
    Y = ((buf[0] & 0x80) >> 7) << 8 | buf[2]
  (9-bit coords on the 360 px round surface). The vendor does not use
  a touch-validity flag; we read 4 bytes and report raw data changes.
*/

#ifndef __TOUCH_H
#define __TOUCH_H

#include <stdint.h>

void Touch_Init(void);                                /* GPIO + bus idle  */
void Touch_Read(uint8_t *buf, uint8_t len);           /* regs 0x00..      */

#endif /* __TOUCH_H */
