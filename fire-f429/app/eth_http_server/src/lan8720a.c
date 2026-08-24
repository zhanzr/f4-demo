/**
  * @file    eth_http_server/src/lan8720a.c
  * @brief   LAN8720A PHY driver (see lan8720a.h).
  *
  * The board PHY is a LAN8720A at address 0, RMII mode. The LAN8720A has an
  * internal power-on reset, so no MCU NRST pin is used here (the reference
  * maps PI1 to ETH_NRST, but PI1 is the MPU6050 INT pin on this board); a
  * software reset via the MDIO BCR register is sufficient. Speed/duplex are
  * read from the PHYSCSR (reg 31, bits 4:2) exactly as the vendor LAN8720A
  * demo does.
  */
#include "lan8720a.h"

int32_t LAN8720_Init(ETH_HandleTypeDef *heth)
{
    uint32_t id1, id2;
    uint32_t timeout = 0;

    /* Software reset + wait for it to complete. */
    LAN8720_WriteReg(heth, LAN8720_REG_BCR, LAN8720_BCR_RESET);
    do
    {
        timeout++;
    } while ((LAN8720_ReadReg(heth, LAN8720_REG_BCR) & LAN8720_BCR_RESET) != 0U &&
             timeout < 10000U);

    /* Restart auto-negotiation. */
    LAN8720_WriteReg(heth, LAN8720_REG_BCR,
                     LAN8720_BCR_AUTONEG | LAN8720_BCR_RESTART_AN);

    /* Read the PHY identifiers (LAN8720A: ID1=0x0007, ID2=0xC0F0). */
    id1 = LAN8720_ReadReg(heth, LAN8720_REG_ID1);
    id2 = LAN8720_ReadReg(heth, LAN8720_REG_ID2);
    if (id1 == 0xFFFFU || id2 == 0xFFFFU)
    {
        return -1;   /* MDIO not responding */
    }
    (void)id1;
    (void)id2;

    return 0;
}

LAN8720_StatusTypeDef LAN8720_GetLinkState(ETH_HandleTypeDef *heth)
{
    uint32_t bsr = LAN8720_ReadReg(heth, LAN8720_REG_BSR);

    if ((bsr & LAN8720_BSR_LINK) == 0U)
    {
        return LAN8720_STATUS_LINK_DOWN;
    }

    /* LAN8720A PHYSCSR bits 4:2 encode speed/duplex. */
    uint32_t scsr = (LAN8720_ReadReg(heth, LAN8720_REG_PHYSCSR) >> 2) & 0x07U;

    switch (scsr)
    {
        case LAN8720_STATUS_100M_FULL:  return LAN8720_STATUS_100MBITS_FULLDUPLEX;
        case LAN8720_STATUS_100M_HALF:  return LAN8720_STATUS_100MBITS_HALFDUPLEX;
        case LAN8720_STATUS_10M_FULL:   return LAN8720_STATUS_10MBITS_FULLDUPLEX;
        case LAN8720_STATUS_10M_HALF:   return LAN8720_STATUS_10MBITS_HALFDUPLEX;
        default:                        return LAN8720_STATUS_ERR;
    }
}