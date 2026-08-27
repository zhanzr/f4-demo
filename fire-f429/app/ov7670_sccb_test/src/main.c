/**
  * @file    ov7670_sccb_test/src/main.c
  * @brief   Minimal SCCB bring-up for the OV7670 on the fire-f429.
  *
  * Step 1: verify ONLY the minimal signal set (user-specified):
  *   Power (VCC/GND - board socket), SCL/SDA (I2C1 PB6/PB7),
  *   PWDN (PG3, low = power on), RST (PG2), XCLK (PA8 = MCO1, HSE).
  *
  * NO LCD, NO DCMI data pins. Console output only (USART1 115200).
  *
  * The OV7670's SCCB logic is clocked by XCLK, so XCLK must be present for
  * the sensor to answer on SCCB at all. We sweep:
  *   - XCLK: 25 MHz (MCO1 /1) and 12.5 MHz (MCO1 /2)
  *   - RST/PWDN polarity (both strappings, since modules differ)
  * and for each combo try to read the product ID (PID 0x76 @ 0x0A,
  * VER 0x73 @ 0x0B) with:
  *   1. hardware I2C1 (PB6/PB7, 100 kHz)
  *   2. a bit-banged SCCB on the same pins (ALIENTEK-style, tolerant)
  * plus a full 7-bit address scan with the bit-bang driver.
  */

#include "board.h"
#include <stdio.h>
#include "sccb_bitbang.h"
#include "stm32f4xx_hal.h"

/* ---------------- minimal pins (the only ones touched) ---------------- */
#define RST_GPIO_PORT   GPIOG
#define RST_GPIO_PIN    GPIO_PIN_2
#define PWDN_GPIO_PORT  GPIOG
#define PWDN_GPIO_PIN   GPIO_PIN_3

#define CAM_I2C         I2C1
#define CAM_DEV_ADDR    0x42        /* OV7670 7-bit 0x21 << 1 */

#define REG_PIDH        0x0A
#define REG_PIDL        0x0B

static I2C_HandleTypeDef h_i2c;

/* ---------------- hardware I2C init (100 kHz, PB6/PB7) ---------------- */
static void hw_i2c_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_I2C1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &gpio);

    h_i2c.Instance = CAM_I2C;
    h_i2c.Init.ClockSpeed = 100000;
    h_i2c.Init.DutyCycle = I2C_DUTYCYCLE_2;
    h_i2c.Init.OwnAddress1 = 0;
    h_i2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    h_i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    h_i2c.Init.OwnAddress2 = 0;
    h_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    h_i2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&h_i2c);
}

/* Read a register over hardware I2C. Returns 0xFF on failure (distinct). */
static uint8_t hw_read_reg(uint8_t addr, uint8_t reg)
{
    uint8_t val = 0xFF;
    if (HAL_I2C_Mem_Read(&h_i2c, addr, (uint16_t)reg, I2C_MEMADD_SIZE_8BIT,
                         &val, 1, 200) == HAL_OK)
    {
        return val;
    }
    return 0xFE;   /* error - no response */
}

/* ---------------- minimal power/reset sequence ----------------------- */
static void cam_power_cycle(uint8_t rst_l2h, uint8_t pwdn_final_high)
{
    /* RST asserted (low), then optionally released high */
    HAL_GPIO_WritePin(RST_GPIO_PORT, RST_GPIO_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PWDN_GPIO_PORT, PWDN_GPIO_PIN,
                      pwdn_final_high ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_Delay(20);
    if (rst_l2h)
    {
        HAL_GPIO_WritePin(RST_GPIO_PORT, RST_GPIO_PIN, GPIO_PIN_SET);
    }
    HAL_Delay(100);
}

/* ---------------- pin init (only the minimal set) -------------------- */
static void cam_pins_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* RST PG2, PWDN PG3: push-pull outputs */
    gpio.Pin = RST_GPIO_PIN | PWDN_GPIO_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOG, &gpio);
}

/* ---------------- the sweep ------------------------------------------ */
int main(void)
{
    HAL_Init();
    Board_Init();                      /* 180 MHz, UART console */

    printf("\r\n=== ov7670_sccb_test v2 (minimal pins + slow BB 50us) ===\r\n");

    cam_pins_init();

    /* XCLK on PA8 = MCO1 (HSE). The scan proved 0x21 ACKs with both 25 MHz
     * and 12.5 MHz; use 12.5 MHz (the ALIENTEK clock assumption). */
    HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_HSE, RCC_MCODIV_2);
    printf("XCLK = 12.5 MHz (MCO1, HSE/2)\r\n");

    /* The scan showed 0x21 ACKs only when RST was released (L2H), so use that. */
    cam_power_cycle(1, 0);             /* rst release L2H, pwdn low (pwr on) */
    printf("RST released (L2H), PWDN low (pwr on)\r\n");

    /* 1) hardware I2C read of a few registers */
    hw_i2c_init();
    {
        uint8_t pidh = hw_read_reg(CAM_DEV_ADDR, REG_PIDH);
        uint8_t pidl = hw_read_reg(CAM_DEV_ADDR, REG_PIDL);
        uint8_t com7 = hw_read_reg(CAM_DEV_ADDR, 0x12);
        printf("[HW I2C 100k] PID=0x%02x VER=0x%02x COM7=0x%02x\r\n",
               (unsigned)pidh, (unsigned)pidl, (unsigned)com7);
    }

    /* 2) slow bit-banged SCCB (50 us) - read several registers */
    SCCB_BB_InitGPIO();
    {
        uint8_t regs[4] = { 0x0A, 0x0B, 0x1C, 0x1D };  /* PID, VER, MIDH, MIDL */
        const char *names[4] = { "PID", "VER", "MIDH", "MIDL" };
        for (int i = 0; i < 4; i++)
        {
            uint8_t v = SCCB_BB_ReadReg(regs[i]);
            printf("[BB SCCB] %s (0x%02x) = 0x%02x\r\n",
                   names[i], (unsigned)regs[i], (unsigned)v);
        }

        /* try a write of COM7=0x80 (reset) then reread PID */
        printf("[BB SCCB] write COM7=0x80 (reset): %s\r\n",
               SCCB_BB_WriteReg(0x12, 0x80) == 0 ? "OK" : "FAIL");
        HAL_Delay(100);
        printf("[BB SCCB] after reset PID=0x%02x VER=0x%02x\r\n",
               (unsigned)SCCB_BB_ReadReg(REG_PIDH),
               (unsigned)SCCB_BB_ReadReg(REG_PIDL));

        /* 3) confirm 0x21 still ACKs + full scan */
        printf("[BB scan] ACK at:");
        for (uint16_t a = 1; a < 128; a++)
        {
            SCCB_BB_Start();
            uint8_t r = SCCB_BB_WriteByte((uint8_t)(a << 1));
            SCCB_BB_Stop();
            if (r == 0)
            {
                printf(" %02x", (unsigned)a);
            }
        }
        printf("\r\n");
    }

    printf("\r\n--- done ---\r\n");
    while (1)
    {
    }
}