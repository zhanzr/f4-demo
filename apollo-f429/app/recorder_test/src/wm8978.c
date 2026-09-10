/*
 * wm8978.c - WM8978 audio codec driver (port of the vendored ALIENTEK Apollo
 * 实验46 example). I2C control on PH4/PH5 (myiic), address 0x1A.
 */
#include "wm8978.h"
#include "sys_compat.h"
#include "myiic.h"
#include "stm32f4xx_hal.h"

static uint16_t regvals[58];

uint8_t WM8978_Write_Reg(uint8_t reg, uint16_t val)
{
    IIC_Start();
    IIC_Send_Byte((WM8978_ADDR << 1) | 0);
    if (IIC_Wait_Ack()) { return 1; }
    IIC_Send_Byte((reg << 1) | ((val >> 8) & 0x01));
    if (IIC_Wait_Ack()) { return 2; }
    IIC_Send_Byte(val & 0xFF);
    if (IIC_Wait_Ack()) { return 3; }
    IIC_Stop();
    regvals[reg] = val;
    return 0;
}

uint16_t WM8978_Read_Reg(uint8_t reg)
{
    return regvals[reg];
}

uint8_t WM8978_Init(void)
{
    IIC_Init();
    if (WM8978_Write_Reg(0, 0)) { return 1; }

    WM8978_Write_Reg(1, 0x1B);    /* MICEN, BIASEN, VMIDSEL=3 */
    WM8978_Write_Reg(2, 0x1B0);   /* ROUT1,LOUT1, BOOSTENR/L */
    WM8978_Write_Reg(3, 0x6C);    /* LOUT2,ROUT2, RMIX,LMIX */
    WM8978_Write_Reg(6, 0);       /* MCLK external */
    WM8978_Write_Reg(43, 1 << 4); /* INVROUT2 */
    WM8978_Write_Reg(47, 1 << 8); /* PGABOOSTL */
    WM8978_Write_Reg(48, 1 << 8); /* PGABOOSTR */
    WM8978_Write_Reg(49, 1 << 1); /* TSDEN */
    WM8978_Write_Reg(49, 1 << 2); /* speaker boost 1.5x */
    WM8978_Write_Reg(10, 1 << 3); /* soft-mute off, 128x oversample */
    WM8978_Write_Reg(14, 1 << 3); /* ADC 128x oversample */
    return 0;
}

void WM8978_ADDA_Cfg(uint8_t dacen, uint8_t adcen)
{
    uint16_t v = WM8978_Read_Reg(3);
    if (dacen) { v |= 3 << 0; } else { v &= ~(3 << 0); }
    WM8978_Write_Reg(3, v);

    v = WM8978_Read_Reg(2);
    if (adcen) { v |= 3 << 0; } else { v &= ~(3 << 0); }
    WM8978_Write_Reg(2, v);
}

void WM8978_Input_Cfg(uint8_t micen, uint8_t lineinen, uint8_t auxen)
{
    uint16_t v = WM8978_Read_Reg(2);
    if (micen) { v |= 3 << 2; } else { v &= ~(3 << 2); }
    WM8978_Write_Reg(2, v);

    v = WM8978_Read_Reg(44);
    if (micen) { v |= 3 << 4 | 3 << 0; } else { v &= ~(3 << 4 | 3 << 0); }
    WM8978_Write_Reg(44, v);

    if (lineinen) { WM8978_LINEIN_Gain(5); } else { WM8978_LINEIN_Gain(0); }
    if (auxen)    { WM8978_AUX_Gain(7); }    else { WM8978_AUX_Gain(0); }
}

void WM8978_Output_Cfg(uint8_t dacen, uint8_t bpsen)
{
    uint16_t v = 0;
    if (dacen) { v |= 1 << 0; }
    if (bpsen) { v |= 1 << 1; v |= 5 << 2; }
    WM8978_Write_Reg(50, v);
    WM8978_Write_Reg(51, v);
}

void WM8978_MIC_Gain(uint8_t gain)
{
    gain &= 0x3F;
    WM8978_Write_Reg(45, gain);
    WM8978_Write_Reg(46, gain | (1 << 8));
}

void WM8978_LINEIN_Gain(uint8_t gain)
{
    uint16_t v;
    gain &= 0x07;
    v = WM8978_Read_Reg(47); v &= ~(7 << 4); WM8978_Write_Reg(47, v | (gain << 4));
    v = WM8978_Read_Reg(48); v &= ~(7 << 4); WM8978_Write_Reg(48, v | (gain << 4));
}

void WM8978_AUX_Gain(uint8_t gain)
{
    uint16_t v;
    gain &= 0x07;
    v = WM8978_Read_Reg(47); v &= ~(7 << 0); WM8978_Write_Reg(47, v | (gain << 0));
    v = WM8978_Read_Reg(48); v &= ~(7 << 0); WM8978_Write_Reg(48, v | (gain << 0));
}

/* fmt: 0 LSB, 1 MSB, 2 standard I2S, 3 PCM/DSP; len: 0 16bit, 1 20, 2 24, 3 32 */
void WM8978_I2S_Cfg(uint8_t fmt, uint8_t len)
{
    WM8978_Write_Reg(4, ((fmt & 3) << 3) | ((len & 3) << 5));
}

void WM8978_HPvol_Set(uint8_t voll, uint8_t volr)
{
    voll &= 0x3F; volr &= 0x3F;
    if (voll == 0) { voll |= 1 << 6; }
    if (volr == 0) { volr |= 1 << 6; }
    WM8978_Write_Reg(52, voll);
    WM8978_Write_Reg(53, volr | (1 << 8));
}

void WM8978_SPKvol_Set(uint8_t volx)
{
    volx &= 0x3F;
    if (volx == 0) { volx |= 1 << 6; }
    WM8978_Write_Reg(54, volx);
    WM8978_Write_Reg(55, volx | (1 << 8));
}

void WM8978_3D_Set(uint8_t depth)
{
    WM8978_Write_Reg(41, depth & 0x0F);
}

void WM8978_EQ_3D_Dir(uint8_t dir)
{
    uint16_t v = WM8978_Read_Reg(0x12);
    if (dir) { v |= 1 << 8; } else { v &= ~(1 << 8); }
    WM8978_Write_Reg(18, v);
}

void WM8978_EQ1_Set(uint8_t cfreq, uint8_t gain)
{
    uint16_t v;
    cfreq &= 3; if (gain > 24) { gain = 24; } gain = 24 - gain;
    v = WM8978_Read_Reg(18); v &= 0x100;
    WM8978_Write_Reg(18, v | ((uint16_t)cfreq << 5) | gain);
}

void WM8978_EQ2_Set(uint8_t cfreq, uint8_t gain)
{
    cfreq &= 3; if (gain > 24) { gain = 24; } gain = 24 - gain;
    WM8978_Write_Reg(19, ((uint16_t)cfreq << 5) | gain);
}

void WM8978_EQ3_Set(uint8_t cfreq, uint8_t gain)
{
    cfreq &= 3; if (gain > 24) { gain = 24; } gain = 24 - gain;
    WM8978_Write_Reg(20, ((uint16_t)cfreq << 5) | gain);
}

void WM8978_EQ4_Set(uint8_t cfreq, uint8_t gain)
{
    cfreq &= 3; if (gain > 24) { gain = 24; } gain = 24 - gain;
    WM8978_Write_Reg(21, ((uint16_t)cfreq << 5) | gain);
}

void WM8978_EQ5_Set(uint8_t cfreq, uint8_t gain)
{
    cfreq &= 3; if (gain > 24) { gain = 24; } gain = 24 - gain;
    WM8978_Write_Reg(22, ((uint16_t)cfreq << 5) | gain);
}