/**
  * @file    eth_http_server/src/dp83848_phy.h
  * @brief   DP83848 10/100 Ethernet PHY driver for the dev1-f407 board (RMII).
  *
  * Wraps the ST DP83848 BSP component (dp83848.c/h) with the same
  * LAN8720A-style interface used by ethernetif.c: Init + GetLinkState.
  *
  * The DP83848 has a 50 MHz active clock generator on the module, so the
  * MCU does NOT drive a 25 MHz crystal/clock to the PHY - the RMII 50 MHz
  * reference clock comes from the PHY itself (PA1 RMII_RX_CLK).
  *
  * PHY address: the DP83848's SMR (reg 0x19) PHY_ADDR field is read to
  * auto-detect the address (DP83848_Init scans 0..31). On this board the
  * address is strapped to 0.
  */
#ifndef __DP83848_PHY_H__
#define __DP83848_PHY_H__

#include "stm32f4xx_hal.h"
#include "dp83848.h"

/* PHY address (strapped on the board). DP83848_Init auto-detects it, but
 * the MDIO read/write helpers need a fixed address - use 0. */
#define DP83848_PHY_ADDRESS     0x00U

typedef enum
{
    DP83848_PHY_LINK_DOWN         = 0,
    DP83848_PHY_100MBITS_FULLDUPLEX,
    DP83848_PHY_100MBITS_HALFDUPLEX,
    DP83848_PHY_10MBITS_FULLDUPLEX,
    DP83848_PHY_10MBITS_HALFDUPLEX,
    DP83848_PHY_ERR
} DP83848_StatusTypeDef;

/* Reset the PHY (soft reset via BCR) and enable auto-negotiation.
 * Returns 0 on success, -1 if MDIO is not responding. */
int32_t DP83848_PhyInit(ETH_HandleTypeDef *heth);

/* Poll the PHY link state. Returns DP83848_STATUS_LINK_DOWN while the cable
 * is unplugged, or the negotiated speed/duplex once the link is up. */
DP83848_StatusTypeDef DP83848_PhyGetLinkState(ETH_HandleTypeDef *heth);

/* The detected PHY address (set by DP83848_PhyInit). */
extern uint32_t dp83848_phy_addr;

/* Read a PHY register through the HAL MDIO interface. */
static inline uint32_t DP83848_ReadReg(ETH_HandleTypeDef *heth, uint32_t reg)
{
    uint32_t val = 0;
    if (HAL_ETH_ReadPHYRegister(heth, dp83848_phy_addr, reg, &val) != HAL_OK)
    {
        return 0xFFFFU;
    }
    return val;
}

static inline void DP83848_WriteReg(ETH_HandleTypeDef *heth, uint32_t reg, uint32_t val)
{
    (void)HAL_ETH_WritePHYRegister(heth, dp83848_phy_addr, reg, val);
}

#endif /* __DP83848_PHY_H__ */