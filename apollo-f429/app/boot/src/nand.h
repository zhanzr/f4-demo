#ifndef _NAND_H
#define _NAND_H

#include <stdint.h>

/* Ported from the ALIENTEK Apollo STM32F429 "实验40 NAND FLASH实验" example
 * (HARDWARE/NAND), types/delay adapted to this repo (stdint + HAL_Delay). */

#define NAND_MAX_PAGE_SIZE            4096
#define NAND_ECC_SECTOR_SIZE          512

typedef struct
{
    uint16_t page_totalsize;    /* page total (main + spare)         */
    uint16_t page_mainsize;     /* page main area                    */
    uint16_t page_sparesize;    /* page spare area                   */
    uint8_t  block_pagenum;     /* pages per block                   */
    uint16_t plane_blocknum;    /* blocks per plane                  */
    uint16_t block_totalnum;    /* total blocks                      */
    uint16_t valid_blocknum;    /* valid (good) blocks               */
    uint32_t id;                /* NAND ID                           */
    uint32_t ecc_hard;          /* hardware ECC value                */
    uint32_t ecc_hdbuf[NAND_MAX_PAGE_SIZE / NAND_ECC_SECTOR_SIZE];
    uint32_t ecc_rdbuf[NAND_MAX_PAGE_SIZE / NAND_ECC_SECTOR_SIZE];
} nand_type;

extern nand_type nand_dev;

/* ---- pin: R/B (ready/busy) on PD6, input ---- */
#define NAND_RB    (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_6))

/* ---- FMC NAND bank 3 window (0x80000000), CMD at bit16, ADDR at bit17 ---- */
#define NAND_ADDRESS  0x80000000UL
#define NAND_CMD      (1UL << 16)
#define NAND_ADDR     (1UL << 17)

/* ---- commands ---- */
#define NAND_READID        0x90
#define NAND_FEATURE       0xEF
#define NAND_RESET         0xFF
#define NAND_READSTA       0x70
#define NAND_AREA_A        0x00
#define NAND_AREA_TRUE1    0x30
#define NAND_WRITE0        0x80
#define NAND_WRITE_TURE1   0x10
#define NAND_ERASE0        0x60
#define NAND_ERASE1        0xD0
#define NAND_MOVEDATA_CMD0 0x00
#define NAND_MOVEDATA_CMD1 0x35
#define NAND_MOVEDATA_CMD2 0x85
#define NAND_MOVEDATA_CMD3 0x10

/* ---- status ---- */
#define NSTA_READY        0x40
#define NSTA_ERROR        0x01
#define NSTA_TIMEOUT      0x02
#define NSTA_ECC1BITERR   0x03
#define NSTA_ECC2BITERR   0x04

/* ---- supported devices ---- */
#define MT29F4G08ABADA   0xDC909556   /* 4Gbit: 2048 B/page, 64 page/blk   */
#define MT29F16G08ABABA  0x48002689   /* 16Gbit: 4096 B/page, 128 page/blk */

uint8_t NAND_Init(void);
uint8_t NAND_ModeSet(uint8_t mode);
uint32_t NAND_ReadID(void);
uint8_t NAND_ReadStatus(void);
uint8_t NAND_WaitForReady(void);
uint8_t NAND_Reset(void);
uint8_t NAND_WaitRB(uint8_t rb);
void NAND_Delay(volatile uint32_t i);
uint8_t NAND_ReadPage(uint32_t PageNum, uint16_t ColNum, uint8_t *pBuffer, uint16_t NumByteToRead);
uint8_t NAND_WritePage(uint32_t PageNum, uint16_t ColNum, uint8_t *pBuffer, uint16_t NumByteToWrite);
uint8_t NAND_EraseBlock(uint32_t BlockNum);
void NAND_EraseChip(void);
uint16_t NAND_ECC_Get_OE(uint8_t oe, uint32_t eccval);
uint8_t NAND_ECC_Correction(uint8_t *data_buf, uint32_t eccrd, uint32_t ecccl);

#endif