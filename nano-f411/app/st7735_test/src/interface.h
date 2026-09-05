/*
  interface.h - low-level ST7735S bus primitives (nano-f411 port).
  Mirrors the vendor example C8T6_md144_t1 (HARDWARE/interface/interface.h),
  trimmed to the 4-wire SPI mode used on this wiring:
    SCL = PA5  (SPI clock, bit-banged)
    SDA = PA6  (SPI MOSI, bit-banged)
    DC  = PA4  (data/command)
    RES = PA7  (reset)
    CS  = PB8  (chip select)
*/

#ifndef __INTERFACE_H
#define __INTERFACE_H

#include <stdint.h>

void CS_SET(void);
void CS_CLR(void);
void WriteComm(uint16_t data);
void WriteData(uint16_t data);
void SendData(uint32_t color);

#endif /* __INTERFACE_H */