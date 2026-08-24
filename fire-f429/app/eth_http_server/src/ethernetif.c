/**
  * @file    eth_http_server/src/ethernetif.c
  * @brief   Ethernet network interface driver for lwIP (NO_SYS / raw API),
  *          fire-f429 board (RMII + LAN8720A PHY).
  *
  * Based on the STM32F769I-Discovery eth_http ethernetif.c, re-targeted to
  * the STM32F429 HAL + the fire-f429 LAN8720A PHY pins.
  *
  * RMII pins (fire-f429, same as the vendor 42-ETH-LWIP demo):
  *   RMII_REF_CLK -> PA1, MDIO -> PA2, MDC -> PC1, CRS_DV -> PA7,
  *   RXD0 -> PC4, RXD1 -> PC5, TX_EN -> PB11, TXD0 -> PG13, TXD1 -> PG14,
  *   PHY_NRST -> PI1
  *
  * The ETH DMA descriptors/RX buffers live in internal SRAM (.sram_dma) -
  * the F429 has no D-cache, so CPU<->DMA are coherent with no cache
  * maintenance and no special memory region.
  */

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"
#include "main.h"
#include "lwip/opt.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"
#include "netif/etharp.h"
#include "ethernetif.h"
#include "lan8720a.h"
#include <stdio.h>
#include <string.h>

/* Private define ------------------------------------------------------------*/
#define IFNAME0 's'
#define IFNAME1 't'

#define ETH_DMA_TRANSMIT_TIMEOUT                (20U)

#ifndef ETH_RX_BUF_SIZE
#define ETH_RX_BUF_SIZE                         1536U
#endif
#ifndef ETH_RX_DESC_CNT
#define ETH_RX_DESC_CNT                         4U
#endif
#ifndef ETH_TX_DESC_CNT
#define ETH_TX_DESC_CNT                         4U
#endif

/* Receive buffers: must be > ETH_RX_DESC_CNT (HAL may need spares); 16
 * absorbs a full TCP window burst (12 segments) without drops. */
#define ETH_RX_BUFFER_CNT                       16U

/* TX bounce buffer: full Ethernet frame (MTU + headers + FCS + margin). */
#define ETH_TX_BUF_SIZE                         (ETH_MAX_PAYLOAD + 32U)

/* Private variables ---------------------------------------------------------*/
typedef enum
{
  RX_ALLOC_OK       = 0x00,
  RX_ALLOC_ERROR    = 0x01
} RxAllocStatusTypeDef;

typedef struct
{
  struct pbuf_custom pbuf_custom;
  uint8_t buff[(ETH_RX_BUF_SIZE + 31) & ~31];
} RxBuff_t;

/* The Ethernet DMA buffers live in internal SRAM (.sram_dma): the ETH DMA
 * can reach it, and there is no D-cache on the F4, so CPU and DMA stay
 * coherent with no cache maintenance. */
__attribute__((section(".sram_dma"), used)) ETH_DMADescTypeDef DMARxDscrTab[ETH_RX_DESC_CNT];
__attribute__((section(".sram_dma"), used)) ETH_DMADescTypeDef DMATxDscrTab[ETH_TX_DESC_CNT];
__attribute__((section(".sram_dma"), used)) static RxBuff_t    rx_buffs[ETH_RX_BUFFER_CNT];
__attribute__((section(".sram_dma"), used)) static uint8_t     tx_bounce[ETH_TX_BUF_SIZE];

/* Simple bitmap allocator for the RX buffers (they are freed out of order). */
static uint16_t rx_used;

static RxBuff_t *rx_buff_alloc(void)
{
  int i;
  for (i = 0; i < ETH_RX_BUFFER_CNT; i++)
  {
    if ((rx_used & (1u << i)) == 0)
    {
      rx_used |= (1u << i);
      return &rx_buffs[i];
    }
  }
  return NULL;
}

static void rx_buff_free(RxBuff_t *b)
{
  rx_used &= ~(1u << (b - rx_buffs));
}

/* Global Ethernet handle */
ETH_HandleTypeDef EthHandle;
ETH_TxPacketConfigTypeDef TxConfig;
static uint8_t     RxAllocStatus;

/* Received-frame counter (link health diagnostics). */
volatile uint32_t eth_rx_cnt;

/* Private function prototypes -----------------------------------------------*/
extern void Error_Handler(void);
int32_t ETH_PHY_IO_Init(void);
int32_t ETH_PHY_IO_DeInit (void);
int32_t ETH_PHY_IO_ReadReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t *pRegVal);
int32_t ETH_PHY_IO_WriteReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t RegVal);
int32_t ETH_PHY_IO_GetTick(void);
void pbuf_free_custom(struct pbuf *p);

/* Private functions ---------------------------------------------------------*/
/*******************************************************************************
                       LL Driver Interface ( LwIP stack --> ETH)
*******************************************************************************/
/**
  * @brief  In this function, the hardware should be initialized.
  *         Called from ethernetif_init().
  * @param  netif the already initialized lwip network interface structure
  */
static void low_level_init(struct netif *netif)
{
  uint8_t macaddress[6] = {ETH_MAC_ADDR0, ETH_MAC_ADDR1, ETH_MAC_ADDR2,
                           ETH_MAC_ADDR3, ETH_MAC_ADDR4, ETH_MAC_ADDR5};

  EthHandle.Instance = ETH;
  EthHandle.Init.MACAddr = macaddress;
  EthHandle.Init.MediaInterface = HAL_ETH_RMII_MODE;
  EthHandle.Init.RxDesc = DMARxDscrTab;
  EthHandle.Init.TxDesc = DMATxDscrTab;
  EthHandle.Init.RxBuffLen = ETH_RX_BUF_SIZE;

  /* Configure ethernet peripheral (GPIOs, clocks, MAC, DMA) */
  if (HAL_ETH_Init(&EthHandle) != HAL_OK)
  {
    Error_Handler();
  }

  /* Set MAC hardware address length */
  netif->hwaddr_len = ETH_HWADDR_LEN;

  /* Set MAC hardware address */
  netif->hwaddr[0] = ETH_MAC_ADDR0;
  netif->hwaddr[1] = ETH_MAC_ADDR1;
  netif->hwaddr[2] = ETH_MAC_ADDR2;
  netif->hwaddr[3] = ETH_MAC_ADDR3;
  netif->hwaddr[4] = ETH_MAC_ADDR4;
  netif->hwaddr[5] = ETH_MAC_ADDR5;

  /* Maximum transfer unit */
  netif->mtu = ETH_MAX_PAYLOAD;

  /* Device capabilities */
  netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;

  /* Set Tx packet config common parameters */
  memset(&TxConfig, 0, sizeof(ETH_TxPacketConfigTypeDef));
  TxConfig.Attributes = ETH_TX_PACKETS_FEATURES_CSUM | ETH_TX_PACKETS_FEATURES_CRCPAD;
  TxConfig.ChecksumCtrl = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC;
  TxConfig.CRCPadCtrl = ETH_CRC_PAD_INSERT;

  /* Initialize the LAN8720A ETH PHY */
  if (LAN8720_Init(&EthHandle) != 0)
  {
    printf("ETH: LAN8720A PHY not responding on MDIO (check PHY wiring)\r\n");
    netif_set_link_down(netif);
    netif_set_down(netif);
    return;
  }
  printf("ETH: LAN8720A PHY OK (ID %04x:%04x)\r\n",
         (unsigned)LAN8720_ReadReg(&EthHandle, LAN8720_REG_ID1),
         (unsigned)LAN8720_ReadReg(&EthHandle, LAN8720_REG_ID2));

  ethernet_link_check_state(netif);
}

/**
  * @brief  Transmit a packet. The pbuf payload lives in SDRAM (lwIP MEM),
  *         which the ETH DMA can reach on the F4, but we still bounce it
  *         through internal SRAM to be safe and descriptor-friendly.
  */
static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
  struct pbuf *q;
  ETH_BufferTypeDef txb;
  uint8_t *dst = tx_bounce;

  if (p->tot_len > ETH_TX_BUF_SIZE)
  {
    return ERR_MEM;
  }

  for (q = p; q != NULL; q = q->next)
  {
    memcpy(dst, q->payload, q->len);
    dst += q->len;
  }

  memset(&TxConfig, 0, sizeof(ETH_TxPacketConfigTypeDef));
  TxConfig.Attributes = ETH_TX_PACKETS_FEATURES_CSUM | ETH_TX_PACKETS_FEATURES_CRCPAD;
  TxConfig.ChecksumCtrl = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC;
  TxConfig.CRCPadCtrl = ETH_CRC_PAD_INSERT;
  TxConfig.Length = p->tot_len;
  txb.buffer = tx_bounce;
  txb.len = p->tot_len;
  txb.next = NULL;
  TxConfig.TxBuffer = &txb;
  TxConfig.pData = NULL;

  if (HAL_ETH_Transmit(&EthHandle, &TxConfig, ETH_DMA_TRANSMIT_TIMEOUT) != HAL_OK)
  {
    return ERR_IF;
  }

  return ERR_OK;
}

/**
  * @brief  Read a received packet into a pbuf (wraps an SRAM RX buffer).
  */
static struct pbuf *low_level_input(struct netif *netif)
{
  struct pbuf *p = NULL;

  if (RxAllocStatus == RX_ALLOC_OK)
  {
    HAL_ETH_ReadData(&EthHandle, (void **)&p);
  }
  if (p != NULL)
  {
    eth_rx_cnt++;
  }
  return p;
}

/**
  * @brief  Poll for received packets and feed them to lwIP.
  */
void ethernetif_input(struct netif *netif)
{
  struct pbuf *p;

  do
  {
    p = low_level_input(netif);
    if (p != NULL)
    {
      if (netif->input(p, netif) != ERR_OK)
      {
        pbuf_free(p);
      }
    }
  } while (p != NULL);
}

/**
  * @brief  lwIP netif init entry point.
  */
err_t ethernetif_init(struct netif *netif)
{
  LWIP_ASSERT("netif != NULL", (netif != NULL));

#if LWIP_NETIF_HOSTNAME
  netif->hostname = "fire-f429";
#endif

  netif->name[0] = IFNAME0;
  netif->name[1] = IFNAME1;
  netif->output = etharp_output;
  netif->linkoutput = low_level_output;

  low_level_init(netif);

  return ERR_OK;
}

/**
  * @brief  Custom Rx pbuf free callback: returns the buffer to the pool.
  */
void pbuf_free_custom(struct pbuf *p)
{
  struct pbuf_custom *custom_pbuf = (struct pbuf_custom *)p;

  rx_buff_free((RxBuff_t *)custom_pbuf);
  if (RxAllocStatus == RX_ALLOC_ERROR)
  {
    RxAllocStatus = RX_ALLOC_OK;
  }
}

/**
  * @brief  Returns the current time in milliseconds (NO_SYS).
  */
u32_t sys_now(void)
{
  return HAL_GetTick();
}

/**
  * @brief  Simple LCG fallback for lwIP's LWIP_RAND() (DNS TXID, DHCP xid).
  */
unsigned int lwip_rand_fallback(void)
{
  static uint32_t seed = 0x12345678U;

  seed = seed * 1664525U + 1013904223U;
  return (unsigned int)(seed >> 16);
}

/*******************************************************************************
                       Ethernet MSP Routines
*******************************************************************************/
/**
  * @brief  Initializes the ETH MSP: clocks + RMII GPIOs (fire-f429).
  */
void HAL_ETH_MspInit(ETH_HandleTypeDef *heth)
{
  GPIO_InitTypeDef GPIO_InitStructure;

  /* Enable GPIOs clocks */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /* Ethernet pins configuration (RMII):
       RMII_REF_CLK  -> PA1
       MDIO          -> PA2
       MDC           -> PC1
       CRS_DV        -> PA7
       RXD0          -> PC4
       RXD1          -> PC5
       TX_EN         -> PB11
       TXD0          -> PG13
       TXD1          -> PG14   */

  GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStructure.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStructure.Pull = GPIO_NOPULL;
  GPIO_InitStructure.Alternate = GPIO_AF11_ETH;

  /* PA1, PA2, PA7 */
  GPIO_InitStructure.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_7;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStructure);

  /* PB11 */
  GPIO_InitStructure.Pin = GPIO_PIN_11;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStructure);

  /* PC1, PC4, PC5 */
  GPIO_InitStructure.Pin = GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStructure);

  /* PG13, PG14 */
  GPIO_InitStructure.Pin = GPIO_PIN_13 | GPIO_PIN_14;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStructure);

  /* Enable ETHERNET clock */
  __HAL_RCC_ETH_CLK_ENABLE();
}

/*******************************************************************************
                       PHY IO Functions
*******************************************************************************/
int32_t ETH_PHY_IO_Init(void)
{
  HAL_ETH_SetMDIOClockRange(&EthHandle);
  return 0;
}

int32_t ETH_PHY_IO_DeInit(void)
{
  return 0;
}

int32_t ETH_PHY_IO_ReadReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t *pRegVal)
{
  if (HAL_ETH_ReadPHYRegister(&EthHandle, DevAddr, RegAddr, pRegVal) != HAL_OK)
  {
    return -1;
  }
  return 0;
}

int32_t ETH_PHY_IO_WriteReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t RegVal)
{
  if (HAL_ETH_WritePHYRegister(&EthHandle, DevAddr, RegAddr, RegVal) != HAL_OK)
  {
    return -1;
  }
  return 0;
}

int32_t ETH_PHY_IO_GetTick(void)
{
  return HAL_GetTick();
}

/**
  * @brief  Re-latch the RMII interface selection.
  *
  * Must be called only while the Ethernet MAC is stopped: on the F4,
  * SYSCFG->PMC's MII/RMII bit routes the 50 MHz RMII reference clock, and
  * toggling it while the MAC runs glitches the clock and drops the PHY
  * link. Called from the link-up path before HAL_ETH_Start().
  */
void eth_rmii_relatch(void)
{
  SYSCFG->PMC &= ~SYSCFG_PMC_MII_RMII_SEL;
  SYSCFG->PMC |= HAL_ETH_RMII_MODE;
  (void)SYSCFG->PMC;
}

/**
  * @brief  Check the PHY link state and (re)start the MAC accordingly.
  */
void ethernet_link_check_state(struct netif *netif)
{
  ETH_MACConfigTypeDef MACConf = {0};
  LAN8720_StatusTypeDef PHYLinkState = LAN8720_GetLinkState(&EthHandle);
  uint32_t linkchanged = 0U, speed = 0U, duplex = 0U;

  if (netif_is_link_up(netif) && (PHYLinkState == LAN8720_STATUS_LINK_DOWN))
  {
    HAL_ETH_Stop(&EthHandle);
    netif_set_down(netif);
    netif_set_link_down(netif);
  }
  else if (!netif_is_link_up(netif) && (PHYLinkState > LAN8720_STATUS_LINK_DOWN))
  {
    switch (PHYLinkState)
    {
      case LAN8720_STATUS_100MBITS_FULLDUPLEX:
        duplex = ETH_FULLDUPLEX_MODE;
        speed = ETH_SPEED_100M;
        linkchanged = 1;
        break;
      case LAN8720_STATUS_100MBITS_HALFDUPLEX:
        duplex = ETH_HALFDUPLEX_MODE;
        speed = ETH_SPEED_100M;
        linkchanged = 1;
        break;
      case LAN8720_STATUS_10MBITS_FULLDUPLEX:
        duplex = ETH_FULLDUPLEX_MODE;
        speed = ETH_SPEED_10M;
        linkchanged = 1;
        break;
      case LAN8720_STATUS_10MBITS_HALFDUPLEX:
        duplex = ETH_HALFDUPLEX_MODE;
        speed = ETH_SPEED_10M;
        linkchanged = 1;
        break;
      default:
        break;
    }

    if (linkchanged)
    {
      /* Re-latch the RMII selection while the MAC is stopped (see
       * eth_rmii_relatch) - never poke SYSCFG->PMC with the MAC running. */
      eth_rmii_relatch();
      HAL_ETH_GetMACConfig(&EthHandle, &MACConf);
      MACConf.DuplexMode = duplex;
      MACConf.Speed = speed;
      HAL_ETH_SetMACConfig(&EthHandle, &MACConf);
      HAL_ETH_Start(&EthHandle);
      netif_set_up(netif);
      netif_set_link_up(netif);
    }
  }
}

void HAL_ETH_RxAllocateCallback(uint8_t **buff)
{
  RxBuff_t *b = rx_buff_alloc();

  if (b != NULL)
  {
    *buff = b->buff;
    b->pbuf_custom.custom_free_function = pbuf_free_custom;
    pbuf_alloced_custom(PBUF_RAW, 0, PBUF_REF, &b->pbuf_custom, *buff, ETH_RX_BUF_SIZE);
  }
  else
  {
    RxAllocStatus = RX_ALLOC_ERROR;
    *buff = NULL;
  }
}

void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff, uint16_t Length)
{
  struct pbuf **ppStart = (struct pbuf **)pStart;
  struct pbuf **ppEnd = (struct pbuf **)pEnd;
  struct pbuf *p = NULL;
  RxBuff_t *b = (RxBuff_t *)((uint8_t *)buff - offsetof(RxBuff_t, buff));

  p = (struct pbuf *)&b->pbuf_custom;
  p->next = NULL;
  p->tot_len = 0;
  p->len = Length;

  if (!*ppStart)
  {
    *ppStart = p;
  }
  else
  {
    (*ppEnd)->next = p;
  }
  *ppEnd = p;

  for (p = *ppStart; p != NULL; p = p->next)
  {
    p->tot_len += Length;
  }
}