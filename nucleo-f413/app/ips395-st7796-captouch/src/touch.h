/*
  touch.h - FT6336U capacitive touch on I2C1 (nucleo-f413 port).

  Wiring: SCL = PB8 (AF4), SDA = PB9 (AF4), RST = PG3, INT = PG2
  (open-drain, active low). The module already carries 10K pull-ups on
  SDA/SCL; slave address 0x38 (7-bit).

  Raw coordinates are mapped onto the display with the macros below.
  Measured on this module: the controller's X/Y axes already run in the
  same direction as the framebuffer (MADCTL 0x48 compensates the panel's
  source-driver side, and the CTP is bonded in the normal viewing
  orientation), so no axis is flipped or swapped. Rebuild with
  -DTP_FLIP_X=1 / -DTP_FLIP_Y=1 / -DTP_SWAP_XY=1 if a marker ever trails
  the opposite way.
*/

#ifndef __TOUCH_H
#define __TOUCH_H

#include <stdint.h>

#ifndef TP_RAW_W
#define TP_RAW_W   320U      /* controller X range: 0 .. W-1 */
#endif
#ifndef TP_RAW_H
#define TP_RAW_H   480U      /* controller Y range: 0 .. H-1 */
#endif
#ifndef TP_SWAP_XY
#define TP_SWAP_XY 0
#endif
#ifndef TP_FLIP_X
#define TP_FLIP_X  0
#endif
#ifndef TP_FLIP_Y
#define TP_FLIP_Y  0
#endif

typedef struct
{
    uint8_t  pressed;        /* 1 = a finger is down                  */
    uint8_t  fingers;        /* controller-reported point count       */
    uint16_t x, y;           /* display coordinates                   */
    uint16_t raw_x, raw_y;   /* controller coordinates (diagnostics)  */
} TouchPoint;

/* Reset + probe the controller. Returns 1 when it answered and was put
 * into interrupt-trigger mode, 0 otherwise (I2C open / not fitted). */
uint8_t Touch_Init(void);
uint8_t Touch_Ok(void);

/* INT pin level: 1 while the controller is signalling a new sample. */
uint8_t Touch_IrqActive(void);

/* Read the current point set into *tp (pressed = 0 when nothing is down). */
void Touch_Scan(TouchPoint *tp);

#endif /* __TOUCH_H */
