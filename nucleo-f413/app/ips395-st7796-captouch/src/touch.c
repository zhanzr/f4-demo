/*
  touch.c - FT6336U capacitive touch on I2C1 (nucleo-f413 port).

  SCL = PB8 (AF4), SDA = PB9 (AF4), RST = PG3, INT = PG2 (active low).
  Slave address 0x38 (7-bit). Register map (FocalTech CTPM):

    0x00  DEVICE_MODE     0 = working mode
    0x01  GEST_ID         gesture id (unused)
    0x02  TD_STATUS       number of touch points in the low nibble
    0x03  TOUCH1_XH ...   six bytes per point: XH, XL, YH, YL, weight, misc
    0x09  TOUCH2_*        (P2 and up are ignored here)
    0xA0  info block      library version, vendor id, firmware id
    0xA4  ID_G_MODE       0 = interrupt trigger, 1 = polling (power-on
                          default); we switch it to trigger mode and still
                          read TD_STATUS, which clears INT

  The demo polls every few milliseconds and takes the INT pin as an
  "immediate" hint, so a stuck INT can never block input.

  Coordinates: the panel is 320x480 and the controller reports in panel
  axes; with MADCTL MX set the display X axis is mirrored, so TP_FLIP_X
  is on by default (see touch.h for the knobs).
*/

#include <stdio.h>
#include "board.h"
#include "lcd.h"
#include "touch.h"

#define TP_ADDR        ((uint16_t)(0x38U << 1))
#define TP_TIMEOUT_MS  10U

static I2C_HandleTypeDef s_i2c;
static uint8_t           s_ok;

/* --------------------------------------------------------------------- */
/* low-level register access                                             */

static uint8_t tp_read(uint8_t reg, uint8_t *buf, uint16_t len)
{
    return (HAL_I2C_Mem_Read(&s_i2c, TP_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                             buf, len, TP_TIMEOUT_MS) == HAL_OK) ? 1U : 0U;
}

static uint8_t tp_write(uint8_t reg, uint8_t val)
{
    return (HAL_I2C_Mem_Write(&s_i2c, TP_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                              &val, 1U, TP_TIMEOUT_MS) == HAL_OK) ? 1U : 0U;
}

/* A NACK or a stuck bus leaves the HAL I2C state machine busy: reset the
 * peripheral (SWRST) and re-init so the next scan starts clean. */
static void tp_bus_recover(void)
{
    __HAL_I2C_DISABLE(&s_i2c);
    I2C1->CR1 |= I2C_CR1_SWRST;
    I2C1->CR1 &= ~I2C_CR1_SWRST;
    __HAL_UNLOCK(&s_i2c);
    s_i2c.State     = HAL_I2C_STATE_RESET;
    s_i2c.ErrorCode = HAL_I2C_ERROR_NONE;
    (void)HAL_I2C_Init(&s_i2c);
}

/* --------------------------------------------------------------------- */
/* HAL MSP: I2C1 pins, INT input, RST output                             */

void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
    GPIO_InitTypeDef g;

    if (hi2c->Instance != I2C1)
    {
        return;
    }

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    /* SCL/SDA: alternate function, open drain, no internal pulls (the
     * module fits 10K on both lines). */
    g.Mode      = GPIO_MODE_AF_OD;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF4_I2C1;
    g.Pin       = TOUCH_SCL_Pin | TOUCH_SDA_Pin;
    HAL_GPIO_Init(GPIOB, &g);

    /* INT: open-drain output on the CTP side, so pull it up here. */
    g.Mode  = GPIO_MODE_INPUT;
    g.Pull  = GPIO_PULLUP;
    g.Pin   = TOUCH_INT_Pin;
    HAL_GPIO_Init(TOUCH_INT_GPIO_Port, &g);

    /* RST: push-pull, idle high (released). */
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Pin   = TOUCH_RST_Pin;
    HAL_GPIO_Init(TOUCH_RST_GPIO_Port, &g);
    HAL_GPIO_WritePin(TOUCH_RST_GPIO_Port, TOUCH_RST_Pin, GPIO_PIN_SET);
}

/* --------------------------------------------------------------------- */

uint8_t Touch_IrqActive(void)
{
    return (HAL_GPIO_ReadPin(TOUCH_INT_GPIO_Port, TOUCH_INT_Pin)
            == GPIO_PIN_RESET) ? 1U : 0U;
}

uint8_t Touch_Ok(void)
{
    return s_ok;
}

uint8_t Touch_Init(void)
{
    uint8_t id[16];

    s_i2c.Instance             = I2C1;
    s_i2c.Init.ClockSpeed      = 400000U;
    s_i2c.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    s_i2c.Init.OwnAddress1     = 0U;
    s_i2c.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    s_i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    s_i2c.Init.OwnAddress2     = 0U;
    s_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_i2c.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&s_i2c) != HAL_OK)
    {
        printf("[TP] I2C1 init failed\r\n");
        return 0U;
    }

    /* Controller reset: low 20 ms, high, then 300 ms to boot. */
    HAL_GPIO_WritePin(TOUCH_RST_GPIO_Port, TOUCH_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(TOUCH_RST_GPIO_Port, TOUCH_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(300);

    if (tp_read(0xA0U, id, sizeof id) == 0U)
    {
        tp_bus_recover();
        s_ok = 0U;
        printf("[TP] no answer at 0x38 (INT=%u) - check PB8/PB9/PG2/PG3\r\n",
               (unsigned)Touch_IrqActive());
        return 0U;
    }

    printf("[TP] info A0..AF:");
    for (uint8_t i = 0U; i < sizeof id; i++)
    {
        printf(" %02X", (unsigned)id[i]);
    }
    printf("\r\n");

    /* Interrupt trigger mode (INT asserts low on a new sample). */
    if (tp_write(0xA4U, 0x00U) == 0U)
    {
        printf("[TP] G_MODE write failed (continuing in polling mode)\r\n");
    }

    s_ok = 1U;
    printf("[TP] FT6336 ready: 400 kHz, INT=%u (idle high = released)\r\n",
           (unsigned)Touch_IrqActive());
    return 1U;
}

void Touch_Scan(TouchPoint *tp)
{
    uint8_t  status = 0U;
    uint8_t  p[6];
    uint32_t ax, ay, aw, ah;
    uint8_t  ev;

    tp->pressed = 0U;
    tp->fingers = 0U;

    if (s_ok == 0U)
    {
        return;
    }

    if (tp_read(0x02U, &status, 1U) == 0U)
    {
        tp_bus_recover();
        return;
    }

    tp->fingers = (uint8_t)(status & 0x0FU);
    if (tp->fingers == 0U)
    {
        return;
    }

    if (tp_read(0x03U, p, 6U) == 0U)
    {
        tp_bus_recover();
        return;
    }

    /* Event flag = bits 7:6 of XH: 0 = press, 1 = release, 2 = contact. */
    ev = (uint8_t)((p[0] >> 6) & 0x03U);
    if ((ev == 1U) || (ev == 3U))
    {
        return;
    }

    tp->raw_x = (uint16_t)(((uint16_t)(p[0] & 0x0FU) << 8) | p[1]);
    tp->raw_y = (uint16_t)(((uint16_t)(p[2] & 0x0FU) << 8) | p[3]);

#if TP_SWAP_XY
    ax = tp->raw_y;  ay = tp->raw_x;
    aw = TP_RAW_H;   ah = TP_RAW_W;
#else
    ax = tp->raw_x;  ay = tp->raw_y;
    aw = TP_RAW_W;   ah = TP_RAW_H;
#endif

    if (ax >= aw) { ax = aw - 1U; }
    if (ay >= ah) { ay = ah - 1U; }

#if TP_FLIP_X
    ax = (aw - 1U) - ax;
#endif
#if TP_FLIP_Y
    ay = (ah - 1U) - ay;
#endif

    tp->x = (uint16_t)((ax * (LCD_Width - 1U)) / (aw - 1U));
    tp->y = (uint16_t)((ay * (LCD_Height - 1U)) / (ah - 1U));
    tp->pressed = 1U;
}
