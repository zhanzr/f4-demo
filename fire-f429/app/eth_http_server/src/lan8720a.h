/**
  * @file    eth_http_server/src/lan8720a.h
  * @brief   Minimal LAN8720A 10/100 Ethernet PHY driver for the fire-f429
  *          board (RMII). Uses HAL_ETH_ReadPHYRegister/WritePHYRegister.
  *
  * PHY address 0 (ADDR pin strapped low), auto-negotiation enabled.
  * Link state + speed/duplex are read from BSR / PHYSCSR (reg 31).
  */
#ifndef __LAN8720A_H__
#define __LAN8720A_H__

#include "stm32f4xx_hal.h"

#define LAN8720_PHY_ADDRESS     0x00U

/* IEEE 802.3 clause-22 registers */
#define LAN8720_REG_BCR         0x00U   /* Basic Control          */
#define LAN8720_REG_BSR         0x01U   /* Basic Status           */
#define LAN8720_REG_ID1         0x02U   /* PHY Identifier 1       */
#define LAN8720_REG_ID2         0x03U   /* PHY Identifier 2       */
#define LAN8720_REG_PHYSCSR     0x1FU   /* LAN8720A Special Ctrl/Status */

/* BCR bits */
#define LAN8720_BCR_RESET       0x8000U
#define LAN8720_BCR_AUTONEG     0x1000U
#define LAN8720_BCR_RESTART_AN  0x0200U

/* BSR bits */
#define LAN8720_BSR_AUTONEG_CMP 0x0020U
#define LAN8720_BSR_LINK        0x0004U

/* PHYSCSR (reg 31) bits 4:2 = speed/duplex */
#define LAN8720_STATUS_10M_HALF   1U
#define LAN8720_STATUS_100M_HALF  2U
#define LAN8720_STATUS_10M_FULL   5U
#define LAN8720_STATUS_100M_FULL  6U

typedef enum
{
    LAN8720_STATUS_LINK_DOWN         = 0,
    LAN8720_STATUS_100MBITS_FULLDUPLEX,
    LAN8720_STATUS_100MBITS_HALFDUPLEX,
    LAN8720_STATUS_10MBITS_FULLDUPLEX,
    LAN8720_STATUS_10MBITS_HALFDUPLEX,
    LAN8720_STATUS_ERR
} LAN8720_StatusTypeDef;

/* Reset the PHY (hardware NRST pulse + soft reset) and enable
 * auto-negotiation. Returns LAN8720_STATUS_OK on success. */
int32_t LAN8720_Init(ETH_HandleTypeDef *heth);

/* Poll the PHY link state. Returns LAN8720_STATUS_LINK_DOWN while the cable
 * is unplugged, or the negotiated speed/duplex once the link is up. */
LAN8720_StatusTypeDef LAN8720_GetLinkState(ETH_HandleTypeDef *heth);

/* Read a PHY register through the HAL MDIO interface. */
static inline uint32_t LAN8720_ReadReg(ETH_HandleTypeDef *heth, uint32_t reg)
{
    uint32_t val = 0;
    if (HAL_ETH_ReadPHYRegister(heth, LAN8720_PHY_ADDRESS, reg, &val) != HAL_OK)
    {
        return 0xFFFFU;
    }
    return val;
}

static inline void LAN8720_WriteReg(ETH_HandleTypeDef *heth, uint32_t reg, uint32_t val)
{
    (void)HAL_ETH_WritePHYRegister(heth, LAN8720_PHY_ADDRESS, reg, val);
}

#endif /* __LAN8720A_H__ */