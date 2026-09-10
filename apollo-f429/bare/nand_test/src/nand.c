/*
  Ported from the ALIENTEK Apollo STM32F429 "实验40 NAND FLASH实验" example
  (HARDWARE/NAND/nand.c): FMC bank-3 NAND low-level driver, adapted to this
  repo (stdint types, HAL_Delay, no malloc/ECC-in-spare priming is kept as in
  the vendor — the self-test in main.c uses the non-ECC path).

  Hardware:
    NAND on FMC NAND bank 3 (NCE3 = PG9), 8-bit data;
    R/B on PD6 (input), data on PD0/1/4/5/11/12/14/15 + PE7/8/9/10.
    Window 0x80000000; CLE -> bit16 (NAND_CMD), ALE -> bit17 (NAND_ADDR).
*/

#include "board.h"
#include "nand.h"
#include "stm32f4xx_hal_nand.h"

NAND_HandleTypeDef NAND_Handler;
nand_type nand_dev;

void HAL_NAND_MspInit(NAND_HandleTypeDef *hnand)
{
    GPIO_InitTypeDef g = {0};
    (void)hnand;

    __HAL_RCC_FMC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* PD6 = R/B input (NAND ready/busy), pull-up. */
    g.Pin = GPIO_PIN_6;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_HIGH;
    HAL_GPIO_Init(GPIOD, &g);

    /* PG9 = NCE3 (chip enable), AF12 FMC. */
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_HIGH;
    g.Alternate = GPIO_AF12_FMC;
    g.Pin = GPIO_PIN_9;
    HAL_GPIO_Init(GPIOG, &g);

    g.Pull = GPIO_NOPULL;
    g.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 |
            GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &g);

    g.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10;
    HAL_GPIO_Init(GPIOE, &g);
}

uint8_t NAND_Init(void)
{
    FMC_NAND_PCC_TimingTypeDef cs, ats;

    NAND_Handler.Instance = FMC_NAND_DEVICE;
    NAND_Handler.Init.NandBank = FMC_NAND_BANK3;
    NAND_Handler.Init.Waitfeature = FMC_NAND_PCC_WAIT_FEATURE_DISABLE;
    NAND_Handler.Init.MemoryDataWidth = FMC_NAND_PCC_MEM_BUS_WIDTH_8;
    NAND_Handler.Init.EccComputation = FMC_NAND_ECC_DISABLE;
    NAND_Handler.Init.ECCPageSize = FMC_NAND_ECC_PAGE_SIZE_2048BYTE;
    NAND_Handler.Init.TCLRSetupTime = 0;
    NAND_Handler.Init.TARSetupTime = 1;

    cs.SetupTime = 2;
    cs.WaitSetupTime = 3;
    cs.HoldSetupTime = 2;
    cs.HiZSetupTime = 1;

    ats.SetupTime = 2;
    ats.WaitSetupTime = 3;
    ats.HoldSetupTime = 2;
    ats.HiZSetupTime = 1;

    HAL_NAND_Init(&NAND_Handler, &cs, &ats);

    NAND_Reset();
    HAL_Delay(100);
    nand_dev.id = NAND_ReadID();
    NAND_ModeSet(4);                       /* MODE4 */

    if (nand_dev.id == MT29F16G08ABABA)
    {
        nand_dev.page_totalsize = 4320;
        nand_dev.page_mainsize  = 4096;
        nand_dev.page_sparesize = 224;
        nand_dev.block_pagenum  = 128;
        nand_dev.plane_blocknum = 2048;
        nand_dev.block_totalnum = 4096;
    }
    else if (nand_dev.id == MT29F4G08ABADA)
    {
        nand_dev.page_totalsize = 2112;
        nand_dev.page_mainsize  = 2048;
        nand_dev.page_sparesize = 64;
        nand_dev.block_pagenum  = 64;
        nand_dev.plane_blocknum = 2048;
        nand_dev.block_totalnum = 4096;
    }
    else
    {
        return 1;
    }
    return 0;
}

uint8_t NAND_ModeSet(uint8_t mode)
{
    *(volatile uint8_t *)(NAND_ADDRESS | NAND_CMD) = NAND_FEATURE;
    *(volatile uint8_t *)(NAND_ADDRESS | NAND_ADDR) = 0x01;
    *(volatile uint8_t *)NAND_ADDRESS = mode;
    *(volatile uint8_t *)NAND_ADDRESS = 0;
    *(volatile uint8_t *)NAND_ADDRESS = 0;
    *(volatile uint8_t *)NAND_ADDRESS = 0;
    return (NAND_WaitForReady() == NSTA_READY) ? 0U : 1U;
}

uint32_t NAND_ReadID(void)
{
    uint8_t id[5];
    uint32_t id32;

    *(volatile uint8_t *)(NAND_ADDRESS | NAND_CMD) = NAND_READID;
    *(volatile uint8_t *)(NAND_ADDRESS | NAND_ADDR) = 0x00;
    id[0] = *(volatile uint8_t *)NAND_ADDRESS;
    id[1] = *(volatile uint8_t *)NAND_ADDRESS;
    id[2] = *(volatile uint8_t *)NAND_ADDRESS;
    id[3] = *(volatile uint8_t *)NAND_ADDRESS;
    id[4] = *(volatile uint8_t *)NAND_ADDRESS;

    id32 = ((uint32_t)id[1] << 24) | ((uint32_t)id[2] << 16) |
           ((uint32_t)id[3] << 8) | id[4];
    return id32;
}

uint8_t NAND_ReadStatus(void)
{
    volatile uint8_t data = 0;
    uint32_t i;

    *(volatile uint8_t *)(NAND_ADDRESS | NAND_CMD) = NAND_READSTA;
    for (i = 0; i < 50; i++) { data++; }   /* keep -O2 from sinking the read */
    data = *(volatile uint8_t *)NAND_ADDRESS;
    return data;
}

uint8_t NAND_WaitForReady(void)
{
    uint32_t time = 0;
    while (1)
    {
        if (NAND_ReadStatus() & NSTA_READY)
        {
            break;
        }
        time++;
        if (time >= 0x1FFFFUL)
        {
            return NSTA_TIMEOUT;
        }
    }
    return NSTA_READY;
}

uint8_t NAND_Reset(void)
{
    *(volatile uint8_t *)(NAND_ADDRESS | NAND_CMD) = NAND_RESET;
    return (NAND_WaitForReady() == NSTA_READY) ? 0U : 1U;
}

uint8_t NAND_WaitRB(uint8_t rb)
{
    uint32_t time = 0;
    while (time < 10000UL)
    {
        time++;
        if (NAND_RB == rb)
        {
            return 0;
        }
    }
    return 1;
}

void NAND_Delay(volatile uint32_t i)
{
    while (i > 0) { i--; }
}

uint8_t NAND_ReadPage(uint32_t PageNum, uint16_t ColNum, uint8_t *pBuffer,
                      uint16_t NumByteToRead)
{
    uint16_t i;
    uint8_t res;

    *(volatile uint8_t *)(NAND_ADDRESS | NAND_CMD) = NAND_AREA_A;
    *(volatile uint8_t *)(NAND_ADDRESS | NAND_ADDR) = (uint8_t)ColNum;
    *(volatile uint8_t *)(NAND_ADDRESS | NAND_ADDR) = (uint8_t)(ColNum >> 8);
    *(volatile uint8_t *)(NAND_ADDRESS | NAND_ADDR) = (uint8_t)PageNum;
    *(volatile uint8_t *)(NAND_ADDRESS | NAND_ADDR) = (uint8_t)(PageNum >> 8);
    *(volatile uint8_t *)(NAND_ADDRESS | NAND_ADDR) = (uint8_t)(PageNum >> 16);
    *(volatile uint8_t *)(NAND_ADDRESS | NAND_CMD) = NAND_AREA_TRUE1;

    res = NAND_WaitRB(0);
    if (res) { return NSTA_TIMEOUT; }
    res = NAND_WaitRB(1);
    if (res) { return NSTA_TIMEOUT; }

    /* Non-ECC path (the self-test reads exactly the main area). */
    for (i = 0; i < NumByteToRead; i++)
    {
        *pBuffer++ = *(volatile uint8_t *)NAND_ADDRESS;
    }

    return (NAND_WaitForReady() == NSTA_READY) ? 0U : NSTA_ERROR;
}

uint8_t NAND_WritePage(uint32_t PageNum, uint16_t ColNum, uint8_t *pBuffer,
                       uint16_t NumByteToWrite)
{
    uint16_t i;

    *(volatile uint8_t *)(NAND_ADDRESS | NAND_CMD) = NAND_WRITE0;
    *(volatile uint8_t *)(NAND_ADDRESS | NAND_ADDR) = (uint8_t)ColNum;
    *(volatile uint8_t *)(NAND_ADDRESS | NAND_ADDR) = (uint8_t)(ColNum >> 8);
    *(volatile uint8_t *)(NAND_ADDRESS | NAND_ADDR) = (uint8_t)PageNum;
    *(volatile uint8_t *)(NAND_ADDRESS | NAND_ADDR) = (uint8_t)(PageNum >> 8);
    *(volatile uint8_t *)(NAND_ADDRESS | NAND_ADDR) = (uint8_t)(PageNum >> 16);
    NAND_Delay(30);                      /* tADL */

    /* Non-ECC path: plain byte write of the main area. */
    for (i = 0; i < NumByteToWrite; i++)
    {
        *(volatile uint8_t *)NAND_ADDRESS = *pBuffer++;
    }

    *(volatile uint8_t *)(NAND_ADDRESS | NAND_CMD) = NAND_WRITE_TURE1;
    return (NAND_WaitForReady() == NSTA_READY) ? 0U : NSTA_ERROR;
}

uint8_t NAND_EraseBlock(uint32_t BlockNum)
{
    if (nand_dev.id == MT29F16G08ABABA)
    {
        BlockNum <<= 7;
    }
    else if (nand_dev.id == MT29F4G08ABADA)
    {
        BlockNum <<= 6;
    }

    *(volatile uint8_t *)(NAND_ADDRESS | NAND_CMD) = NAND_ERASE0;
    *(volatile uint8_t *)(NAND_ADDRESS | NAND_ADDR) = (uint8_t)BlockNum;
    *(volatile uint8_t *)(NAND_ADDRESS | NAND_ADDR) = (uint8_t)(BlockNum >> 8);
    *(volatile uint8_t *)(NAND_ADDRESS | NAND_ADDR) = (uint8_t)(BlockNum >> 16);
    *(volatile uint8_t *)(NAND_ADDRESS | NAND_CMD) = NAND_ERASE1;

    return (NAND_WaitForReady() == NSTA_READY) ? 0U : NSTA_ERROR;
}

void NAND_EraseChip(void)
{
    uint16_t i;
    for (i = 0; i < nand_dev.block_totalnum; i++)
    {
        (void)NAND_EraseBlock(i);
    }
}