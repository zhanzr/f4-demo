/*
 * pcf8574.h - PCF8574T I/O expander (vendored Apollo example).
 *
 * PB12 is a SHARED pin: it is the PCF8574 INT output AND the DHT11 data pin.
 * The vendor doc (实验33 readme) states the DHT11 and DS18B20 connectors share
 * this pin, and before reading the DHT11 you must do one PCF8574T read so the
 * expander stops holding IIC_INT (PB12) low - otherwise the DHT11 line reads
 * stuck-low and the sensor appears absent.
 */
#ifndef __PCF8574_H__
#define __PCF8574_H__

#include <stdint.h>

#define PCF8574_INT  PBin(12)   /* PCF8574 INT output -> PB12 */

#define PCF8574_ADDR   0x40     /* PCF8574 address (8-bit, R/W in bit 0) */

/* PCF8574 port mapping on this board. */
#define BEEP_IO       0   /* buzzer                     P0 */
#define AP_INT_IO     1   /* AP3216C interrupt          P1 */
#define DCMI_PWDN_IO  2   /* DCMI power-down            P2 */
#define USB_PWR_IO    3   /* USB power control          P3 */
#define EX_IO         4   /* spare expander IO          P4 */
#define MPU_INT_IO    5   /* MPU9250 interrupt          P5 */
#define RS485_RE_IO   6   /* RS485 RE/DIR               P6 */
#define ETH_RESET_IO  7   /* ethernet PHY reset         P7 */

uint8_t PCF8574_Init(void);
uint8_t PCF8574_ReadOneByte(void);
void    PCF8574_WriteOneByte(uint8_t DataToWrite);
void    PCF8574_WriteBit(uint8_t bit, uint8_t sta);
uint8_t PCF8574_ReadBit(uint8_t bit);

/* Release the PCF8574 INT line on PB12 so the shared DHT11 pin can be used. */
#define PCF8574_ReleaseINT()   PCF8574_ReadBit(BEEP_IO)

#endif /* __PCF8574_H__ */