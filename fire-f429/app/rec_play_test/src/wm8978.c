/*
 * WM8978 audio codec driver (I2C control interface) - fire-f429 board.
 *
 * Ported from the Wildfire F429 "37-I2S_audio" example (bsp_wm8978.c) and
 * trimmed to what the rec_play test needs: init/reset, audio-path config
 * (MIC->ADC for recording, DAC->earphone/speaker for playback), gains and
 * the I2S audio interface format. Register write-only (the WM8978 I2C
 * interface cannot be read back), so a shadow register cache is kept.
 *
 * Control bus: I2C1 PB6 (SCL) / PB7 (SDA), 400 kHz, 7-bit address 0x34.
 * Audio bus:   I2S2 (see rec_play.c).
 */
#include "wm8978.h"
#include <string.h>

#define WM8978_SLAVE_ADDRESS   0x34U
#define WM8978_I2C_TIMEOUT     50U

static I2C_HandleTypeDef codec_i2c;

/* Register shadow cache (defaults from the WM8978 datasheet, R0..R57). */
static uint16_t wm8978_reg[58] = {
    0x000, 0x000, 0x000, 0x000, 0x050, 0x000, 0x140, 0x000,
    0x000, 0x000, 0x000, 0x0FF, 0x0FF, 0x000, 0x100, 0x0FF,
    0x0FF, 0x000, 0x12C, 0x02C, 0x02C, 0x02C, 0x02C, 0x000,
    0x032, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
    0x038, 0x00B, 0x032, 0x000, 0x008, 0x00C, 0x093, 0x0E9,
    0x000, 0x000, 0x000, 0x000, 0x003, 0x010, 0x010, 0x100,
    0x100, 0x002, 0x001, 0x001, 0x039, 0x039, 0x039, 0x039,
    0x001, 0x001
};

/* --- I2C plumbing --------------------------------------------------------- */

static HAL_StatusTypeDef Codec_I2C_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    gpio.Pin       = GPIO_PIN_6 | GPIO_PIN_7;      /* PB6=SCL, PB7=SDA */
    gpio.Mode      = GPIO_MODE_AF_OD;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &gpio);

    codec_i2c.Instance             = I2C1;
    codec_i2c.Init.ClockSpeed      = 400000U;
    codec_i2c.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    codec_i2c.Init.OwnAddress1     = 0U;
    codec_i2c.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    codec_i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    codec_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    codec_i2c.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    return HAL_I2C_Init(&codec_i2c);
}

/* WM8978 control word: 9-bit register value, B8..B1 = reg addr, B0 = MSB. */
static void WM8978_WriteReg(uint8_t reg, uint16_t val)
{
    uint8_t tmp[2];

    tmp[0] = (uint8_t)(((reg & 0x7FU) << 1) | ((val >> 8) & 1U));
    tmp[1] = (uint8_t)(val & 0xFFU);
    HAL_I2C_Master_Transmit(&codec_i2c, WM8978_SLAVE_ADDRESS, tmp, 2U,
                            WM8978_I2C_TIMEOUT);
    wm8978_reg[reg] = val;
}

/* Init I2C and reset the codec. Returns 1 on success. */
int WM8978_Init(void)
{
    if (Codec_I2C_Init() != HAL_OK)
    {
        return 0;
    }
    /* The WM8978 has no read-back; probe the bus by polling the address. */
    if (HAL_I2C_IsDeviceReady(&codec_i2c, WM8978_SLAVE_ADDRESS, 3U,
                              WM8978_I2C_TIMEOUT) != HAL_OK)
    {
        return 0;                     /* no ACK: codec not on the bus */
    }
    return WM8978_Reset();
}

/* Reset all codec registers to their power-on defaults. */
int WM8978_Reset(void)
{
    static const uint16_t reset_value[58] = {
        0x000, 0x000, 0x000, 0x000, 0x050, 0x000, 0x140, 0x000,
        0x000, 0x000, 0x000, 0x0FF, 0x0FF, 0x000, 0x100, 0x0FF,
        0x0FF, 0x000, 0x12C, 0x02C, 0x02C, 0x02C, 0x02C, 0x000,
        0x032, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
        0x038, 0x00B, 0x032, 0x000, 0x008, 0x00C, 0x093, 0x0E9,
        0x000, 0x000, 0x000, 0x000, 0x003, 0x010, 0x010, 0x100,
        0x100, 0x002, 0x001, 0x001, 0x039, 0x039, 0x039, 0x039,
        0x001, 0x001
    };

    WM8978_WriteReg(0, 0);                 /* write R0 -> full reset */
    memcpy(wm8978_reg, reset_value, sizeof(reset_value));
    HAL_Delay(10);                         /* let the codec settle */
    return 1;
}

/* --- Audio path / analogue setup (as in the vendor example) ---------------- */

void WM8978_CfgAudioPath(uint16_t in_path, uint16_t out_path)
{
    uint16_t reg;

    if ((in_path == IN_PATH_OFF) && (out_path == OUT_PATH_OFF))
    {
        WM8978_Reset();
        return;
    }

    /* R1: power management 1 */
    reg = (1U << 3) | (3U << 0);           /* BIASEN | VMIDSEL=11 */
    if (out_path & OUT3_4_ON) reg |= (1U << 7) | (1U << 6);
    if ((in_path & MIC_LEFT_ON) || (in_path & MIC_RIGHT_ON)) reg |= (1U << 4);
    WM8978_WriteReg(1, reg);

    /* R2: power management 2 */
    reg = 0;
    if (out_path & EAR_LEFT_ON)  reg |= (1U << 7);
    if (out_path & EAR_RIGHT_ON) reg |= (1U << 8);
    if (in_path & MIC_LEFT_ON)   reg |= (1U << 4) | (1U << 2);
    if (in_path & MIC_RIGHT_ON)  reg |= (1U << 5) | (1U << 3);
    if (in_path & LINE_ON)       reg |= (1U << 4) | (1U << 5);
    if (in_path & ADC_ON)        reg |= (1U << 1) | (1U << 0);
    WM8978_WriteReg(2, reg);

    /* R3: power management 3 */
    reg = 0;
    if (out_path & OUT3_4_ON) reg |= (1U << 8) | (1U << 7);
    if (out_path & SPK_ON)    reg |= (1U << 6) | (1U << 5);
    if (out_path != OUT_PATH_OFF) reg |= (1U << 3) | (1U << 2);
    if (in_path & DAC_ON)     reg |= (1U << 1) | (1U << 0);
    WM8978_WriteReg(3, reg);

    /* R44: input control (MIC differential to the input PGA) */
    reg = 0;
    if (in_path & LINE_ON)      reg |= (1U << 6) | (1U << 2);
    if (in_path & MIC_RIGHT_ON) reg |= (1U << 5) | (1U << 4);
    if (in_path & MIC_LEFT_ON)  reg |= (1U << 1) | (1U << 0);
    WM8978_WriteReg(44, reg);

    /* R14: ADC control - exactly the vendor example value: HPF disabled,
     * audio mode, 128x oversampling (best performance). DC is removed
     * digitally per chunk in RecPlay_PostProcess() instead. */
    WM8978_WriteReg(14, (in_path & ADC_ON) ? ((1U << 3) | 4U) : 0U);

    /* R32..R34: automatic level control off */
    WM8978_WriteReg(32, 0);
    WM8978_WriteReg(33, 0);
    WM8978_WriteReg(34, 0);
    WM8978_WriteReg(35, (3U << 1) | 7U);   /* noise gate off */

    /* R47/R48: input boost (+20 dB from MIC) */
    reg = 0;
    if ((in_path & MIC_LEFT_ON) || (in_path & MIC_RIGHT_ON)) reg |= (1U << 8);
    if (in_path & AUX_ON) reg |= (3U << 0);
    if (in_path & LINE_ON) reg |= (3U << 4);
    WM8978_WriteReg(47, reg);
    WM8978_WriteReg(48, reg);

    /* R15/R16: ADC digital gain 0 dB */
    WM8978_WriteReg(15, 0xFFU);
    WM8978_WriteReg(16, 0x1FFU);

    /* R43: beep / ROUT2 inverter */
    reg = 0;
    if (out_path & SPK_ON) reg |= (1U << 4);
    if (in_path & AUX_ON)  reg |= (7U << 1) | (1U << 0);
    WM8978_WriteReg(43, reg);

    /* R49: output control */
    reg = 0;
    if (in_path & DAC_ON)  reg |= (1U << 6) | (1U << 5);
    if (out_path & SPK_ON) reg |= (1U << 2) | (1U << 1);
    if (out_path & OUT3_4_ON) reg |= (1U << 4) | (1U << 3);
    WM8978_WriteReg(49, reg);

    /* R50/R51: output mixers */
    reg = 0;
    if (in_path & AUX_ON) reg |= (7U << 6) | (1U << 5);
    if ((in_path & LINE_ON) || (in_path & MIC_LEFT_ON) || (in_path & MIC_RIGHT_ON))
        reg |= (7U << 2) | (1U << 1);
    if (in_path & DAC_ON) reg |= (1U << 0);
    WM8978_WriteReg(50, reg);
    WM8978_WriteReg(51, reg);

    /* R56/R57: OUT3/OUT4 mixers (unused here) */
    WM8978_WriteReg(56, 0);
    WM8978_WriteReg(57, 0);

    /* R11/R12: DAC digital volume */
    if (in_path & DAC_ON)
    {
        WM8978_WriteReg(11, 0xFFU);
        WM8978_WriteReg(12, 0x1FFU);
    }
    else
    {
        WM8978_WriteReg(11, 0);
        WM8978_WriteReg(12, 0x100U);
    }

    /* R10: DAC control (no soft mute) */
    if (in_path & DAC_ON)
    {
        WM8978_WriteReg(10, 0);
    }
}

/* Configure the codec-side I2S format: Philips standard (FMT=10),
 * 16-bit word length, slave (MCLK from the MCU master). */
void WM8978_CfgAudioIF(void)
{
    WM8978_WriteReg(4, (2U << 3));          /* FMT = I2S, WL = 16 bit */
    WM8978_WriteReg(6, 0x000);              /* MS = 0: codec is slave */
}

void WM8978_SetMicGain(uint8_t gain)        /* 0..63 */
{
    if (gain > 63U) gain = 63U;
    WM8978_WriteReg(45, gain);              /* left input PGA */
    WM8978_WriteReg(46, gain | (1U << 8));  /* right, zero-cross */
}

void WM8978_SetOUT1Volume(uint8_t vol)      /* earphone, 0..63 */
{
    if (vol > 63U) vol = 63U;
    WM8978_WriteReg(52, vol);
    WM8978_WriteReg(53, vol | (1U << 8));
}

void WM8978_SetOUT2Volume(uint8_t vol)      /* speaker, 0..63 */
{
    if (vol > 63U) vol = 63U;
    WM8978_WriteReg(54, vol);
    WM8978_WriteReg(55, vol | (1U << 8));
}
