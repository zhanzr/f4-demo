#ifndef SCCB_BITBANG_H
#define SCCB_BITBANG_H

#include <stdint.h>

/* Result codes, distinct from sensor data values. */
#define SCCB_BB_ACK_FAIL   0xFF    /* no ACK on the addressed byte          */
#define SCCB_BB_TIMEOUT    0xFE    /* bus stuck (shouldn't happen w/ GPIO)  */

/* Minimal SCCB (I2C-like) bit-bang driver used during OV7670 bring-up.
 * Same electrical protocol as I2C (SCCB isn't standard I2C but on this
 * 2-wire bus the difference is negligible). Uses PB6 = SCL, PB7 = SDA,
 * 50 us half-clock (like the ALIENTEK reference), SDA open-drain with
 * internal pull-up.
 *
 * These functions own the GPIO entirely (AF cleared), so they don't
 * conflict with the hardware I2C peripheral as long as they're used
 * exclusively (call BB_Reinit_GPIO() first if you've used the HW I2C). */

void    SCCB_BB_InitGPIO(void);     /* PB6 SCL / PB7 SDA as open-drain out */
void    SCCB_BB_Start(void);
void    SCCB_BB_Stop(void);
uint8_t SCCB_BB_WriteByte(uint8_t dat);   /* 0 OK, SCCB_BB_ACK_FAIL if NACK */
uint8_t SCCB_BB_ReadByte(void);           /* returns the byte               */
uint8_t SCCB_BB_ReadReg(uint8_t reg);     /* sensor value, or ACK_FAIL      */
uint8_t SCCB_BB_WriteReg(uint8_t reg, uint8_t val);

#endif /* SCCB_BITBANG_H */