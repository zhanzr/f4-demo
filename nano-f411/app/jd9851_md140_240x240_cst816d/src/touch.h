/*
  touch.h - CST816D capacitive touch controller (I2C, bit-banged) for the
  nv3030b_md183_240x284_cst816d project.

  Wiring: TOUCH_SCL = PA2 (push-pull host output)
          TOUCH_SDA = PA3 (open-drain, released to the slave for reads)

  Protocol (vendored TK018F3716 example, ported): the touch data block is
  read from register 0x00 (8 bytes). Byte 3 = 0x80 marks an active touch;
  byte 4 = X (8 bit, panel is 240 wide), bytes 5:6 = Y (12 bit, panel is
  284 tall).
*/

#ifndef __TOUCH_H
#define __TOUCH_H

#include <stdint.h>

void Touch_Init(void);                                /* GPIO + bus idle  */
void Touch_Read(uint8_t *buf, uint8_t len);           /* regs 0x00..      */

#endif /* __TOUCH_H */