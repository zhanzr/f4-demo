/**
  * @file    eth_http_server/src/dp83848_phy.c
  * @brief   DP83848 PHY driver wrapper (see dp83848_phy.h).
  *
  * Uses the ST DP83848 BSP component (dp83848.c) for init + link state.
  * The DP83848 has a 50 MHz active clock generator, so no MCU clock output
  * is needed; the PHY drives the RMII 50 MHz reference clock on PA1.
  */
#include "dp83848_phy.h"
#include <stdio.h>

/* The global ETH handle (defined in ethernetif.c). */
extern ETH_HandleTypeDef EthHandle;

/* The detected PHY address (used by the DP83848_ReadReg/WriteReg helpers). */
uint32_t dp83848_phy_addr = DP83848_PHY_ADDRESS;

/* The ST BSP component needs an IO context (MDIO read/write + tick). */
static dp83848_Object_t dp83848_obj;

static int32_t phy_io_init(void)
{
    /* Set the MDIO clock range (must be done before any MDIO access). */
    HAL_ETH_SetMDIOClockRange(&EthHandle);
    return 0;
}
static int32_t phy_io_deinit(void)    { return 0; }

static int32_t phy_io_read(uint32_t dev, uint32_t reg, uint32_t *val)
{
    if (HAL_ETH_ReadPHYRegister(&EthHandle, dev, reg, val) != HAL_OK)
    {
        return -1;
    }
    return 0;
}

static int32_t phy_io_write(uint32_t dev, uint32_t reg, uint32_t val)
{
    if (HAL_ETH_WritePHYRegister(&EthHandle, dev, reg, val) != HAL_OK)
    {
        return -1;
    }
    return 0;
}

static int32_t phy_io_tick(void)
{
    return (int32_t)HAL_GetTick();
}

int32_t DP83848_PhyInit(ETH_HandleTypeDef *heth)
{
    (void)heth;

    /* Set the MDIO clock range (must be done before any MDIO access). */
    HAL_ETH_SetMDIOClockRange(&EthHandle);

    /* Register the MDIO IO functions with the BSP component. */
    dp83848_IOCtx_t io = {
        .Init     = phy_io_init,
        .DeInit   = phy_io_deinit,
        .ReadReg  = phy_io_read,
        .WriteReg = phy_io_write,
        .GetTick  = phy_io_tick,
    };
    if (DP83848_RegisterBusIO(&dp83848_obj, &io) != DP83848_STATUS_OK)
    {
        return -1;
    }

    /* Scan for the DP83848: read the PHY ID1 (reg 2) at each address. The
     * DP83848 ID1 = 0x2000. Skip addresses that return 0xFFFF (no PHY). */
    uint32_t found = 0;
    for (uint32_t addr = 0; addr <= 31; addr++)
    {
        uint32_t id1 = 0;
        if (HAL_ETH_ReadPHYRegister(&EthHandle, addr, DP83848_PHYI1R, &id1) == HAL_OK)
        {
            if (id1 != 0xFFFFU && id1 != 0U)
            {
                dp83848_obj.DevAddr = addr;
                dp83848_obj.Is_Initialized = 1;
                dp83848_phy_addr = addr;
                found = 1;
                printf("ETH: DP83848 found at addr %lu (ID1 0x%04lx)\r\n",
                       (unsigned long)addr, (unsigned long)id1);
                break;
            }
        }
    }
    if (!found)
    {
        /* Fall back to address 0. */
        dp83848_obj.DevAddr = DP83848_PHY_ADDRESS;
        dp83848_obj.Is_Initialized = 1;
    }

    /* Diagnostic: read the RMII and Bypass Register (RBR, 0x17) to check
     * the RMII_MODE bit (bit 5). 0 = MII, 1 = RMII. The DP83848 defaults
     * to MII via the strap, but RMII_MODE is "Strap, RW" - software can
     * write it. If the PHY is in MII mode, the MAC (configured for RMII)
     * cannot communicate with it, so we must switch it to RMII here.
     * Also report RX_OVF_STS (bit 3) / RX_UNF_STS (bit 2) - elasticity
     * buffer overflow/underflow, which indicate an RMII clock frequency
     * mismatch with the recovered data clock. */
    {
        uint32_t rbr = DP83848_ReadReg(heth, 0x17U);
        printf("ETH: RBR 0x%04lx (RMII_MODE=%lu RX_OVF=%lu RX_UNF=%lu)\r\n",
               (unsigned long)rbr, (unsigned long)((rbr >> 5) & 1U),
               (unsigned long)((rbr >> 3) & 1U), (unsigned long)((rbr >> 2) & 1U));
    }

    /* Switch the PHY to RMII mode: set RBR bit 5 (RMII_MODE). The strap
     * defaults it to MII, but the bit is software-writable. This MUST be
     * done before auto-negotiation so the PHY drives the RMII signals
     * (RXD[1:0], CRS_DV) synchronously to the 50 MHz X1 clock. */
    {
        uint32_t rbr = DP83848_ReadReg(heth, 0x17U);
        rbr |= (1U << 5);   /* RMII_MODE = 1 */
        DP83848_WriteReg(heth, 0x17U, rbr);
        rbr = DP83848_ReadReg(heth, 0x17U);
        printf("ETH: RBR after RMII set 0x%04lx (RMII_MODE=%lu)\r\n",
               (unsigned long)rbr, (unsigned long)((rbr >> 5) & 1U));
    }

    /* Soft reset the PHY (BCR bit 15) and wait for it to clear. */
    DP83848_WriteReg(heth, DP83848_BCR, DP83848_BCR_SOFT_RESET);
    {
        uint32_t t0 = HAL_GetTick();
        while ((DP83848_ReadReg(heth, DP83848_BCR) & DP83848_BCR_SOFT_RESET) != 0U)
        {
            if ((HAL_GetTick() - t0) > 100U) break;
        }
    }

    /* Bring the PHY out of power-down. */
    DP83848_DisablePowerDownMode(&dp83848_obj);

    /* NOTE: no ANAR write here. The vendor example (stm32f4x7_eth.c
     * ETH_Init) does NOT touch ANAR - the DP83848's reset default
     * (0x01E1) already advertises 10BT/10BT-FD/100TX/100TX-FD with the
     * correct IEEE 802.3 selector field (0x0001). Writing ANAR with the
     * ST BSP's DP83848_ANAR_SELECTOR_FIELD (0x000F) puts an invalid
     * selector (01111) in bits 4:0, which can confuse the auto-negotiation
     * state machine and make the link flap. */

    /* Vendor sequence (stm32f4x7_eth.c ETH_Init): wait for linked status,
     * then enable auto-negotiation, then wait for it to complete. */
    {
        uint32_t t0 = HAL_GetTick();
        while ((DP83848_ReadReg(heth, DP83848_BSR) & DP83848_BSR_LINK_STATUS) == 0U)
        {
            if ((HAL_GetTick() - t0) > 3000U) break;
        }
    }

    DP83848_StartAutoNego(&dp83848_obj);

    {
        uint32_t t0 = HAL_GetTick();
        while ((DP83848_ReadReg(heth, DP83848_BSR) & DP83848_BSR_AUTONEGO_CPLT) == 0U)
        {
            if ((HAL_GetTick() - t0) > 3000U) break;
        }
    }

    /* Diagnostic: report the auto-negotiation advertisement (ANAR, 0x04)
     * and link partner ability (ANLPAR, 0x05) to see what speed/duplex
     * was negotiated. */
    {
        uint32_t anar = DP83848_ReadReg(heth, 0x04U);
        uint32_t anlpar = DP83848_ReadReg(heth, 0x05U);
        uint32_t bsr = DP83848_ReadReg(heth, DP83848_BSR);
        uint32_t sr = DP83848_ReadReg(heth, DP83848_PHYSCSR);
        printf("ETH: ANAR 0x%04lx ANLPAR 0x%04lx BSR 0x%04lx PHYSCSR 0x%04lx\r\n",
               (unsigned long)anar, (unsigned long)anlpar,
               (unsigned long)bsr, (unsigned long)sr);
    }

    return 0;
}

DP83848_StatusTypeDef DP83848_PhyGetLinkState(ETH_HandleTypeDef *heth)
{
    /* Read the DP83848 PHYSCSR (reg 0x10) directly, using the vendor's
     * bit interpretation (stm32f4x7_eth_conf.h):
     *   bit 1 (0x0002) = speed: 0 = 100M, 1 = 10M
     *   bit 2 (0x0004) = duplex: 1 = full, 0 = half
     * This matches the DP83848 datasheet and avoids the ST BSP's
     * different PHYSCSR bit layout. */
    uint32_t bsr;

    /* BSR bit 2 (LINK_STATUS) is a latching-low bit: it reads 0 if the
     * link has gone down at any point since the last read, and reading
     * the register clears the latch. Read it twice like the ST BSP does
     * (DP83848_GetLinkState) - the first read clears the latch, the
     * second gives the current state. A single read would report false
     * LINK_DOWN after any transient glitch (e.g. during autoneg). */
    (void)DP83848_ReadReg(heth, DP83848_BSR);   /* clear the latch */
    bsr = DP83848_ReadReg(heth, DP83848_BSR);   /* current state */
    if ((bsr & DP83848_BSR_LINK_STATUS) == 0U)
    {
        return DP83848_PHY_LINK_DOWN;
    }

    uint32_t sr = DP83848_ReadReg(heth, DP83848_PHYSCSR);
    uint32_t speed10 = (sr & 0x0002U) ? 1U : 0U;   /* 1 = 10M, 0 = 100M */
    uint32_t full   = (sr & 0x0004U) ? 1U : 0U;    /* 1 = full, 0 = half */

    if (speed10)
    {
        return full ? DP83848_PHY_10MBITS_FULLDUPLEX : DP83848_PHY_10MBITS_HALFDUPLEX;
    }
    return full ? DP83848_PHY_100MBITS_FULLDUPLEX : DP83848_PHY_100MBITS_HALFDUPLEX;
}