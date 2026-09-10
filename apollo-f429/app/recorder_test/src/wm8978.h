/*
 * wm8978.h - WM8978 audio codec driver (clean port of the vendored ALIENTEK
 * Apollo 实验46 recorder example). Control interface is the bit-banged I2C on
 * PH4 (SCL) / PH5 (SDA) (myiic), device address 0x1A; audio data flows over
 * SAI1 (PE2-6).
 */
#ifndef __WM8978_H
#define __WM8978_H

#include <stdint.h>

#define WM8978_ADDR     0x1A

/* EQ center-frequency selects */
#define EQ1_80Hz  0x00
#define EQ1_105Hz 0x01
#define EQ1_135Hz 0x02
#define EQ1_175Hz 0x03
#define EQ2_230Hz 0x00
#define EQ2_300Hz 0x01
#define EQ2_385Hz 0x02
#define EQ2_500Hz 0x03
#define EQ3_650Hz  0x00
#define EQ3_850Hz  0x01
#define EQ3_1100Hz 0x02
#define EQ3_1400Hz 0x03
#define EQ4_1800Hz 0x00
#define EQ4_2400Hz 0x01
#define EQ4_3200Hz 0x02
#define EQ4_4100Hz 0x03
#define EQ5_5300Hz 0x00
#define EQ5_6900Hz 0x01
#define EQ5_9000Hz 0x02
#define EQ5_11700Hz 0x03

uint8_t WM8978_Init(void);
void    WM8978_ADDA_Cfg(uint8_t dacen, uint8_t adcen);
void    WM8978_Input_Cfg(uint8_t micen, uint8_t lineinen, uint8_t auxen);
void    WM8978_Output_Cfg(uint8_t dacen, uint8_t bpsen);
void    WM8978_MIC_Gain(uint8_t gain);
void    WM8978_LINEIN_Gain(uint8_t gain);
void    WM8978_AUX_Gain(uint8_t gain);
uint8_t WM8978_Write_Reg(uint8_t reg, uint16_t val);
uint16_t WM8978_Read_Reg(uint8_t reg);
void    WM8978_HPvol_Set(uint8_t voll, uint8_t volr);
void    WM8978_SPKvol_Set(uint8_t volx);
void    WM8978_I2S_Cfg(uint8_t fmt, uint8_t len);
void    WM8978_3D_Set(uint8_t depth);
void    WM8978_EQ_3D_Dir(uint8_t dir);
void    WM8978_EQ1_Set(uint8_t cfreq, uint8_t gain);
void    WM8978_EQ2_Set(uint8_t cfreq, uint8_t gain);
void    WM8978_EQ3_Set(uint8_t cfreq, uint8_t gain);
void    WM8978_EQ4_Set(uint8_t cfreq, uint8_t gain);
void    WM8978_EQ5_Set(uint8_t cfreq, uint8_t gain);

#endif /* __WM8978_H */