/************************************************
  apollo-f429 "bare" nand_test - FMC NAND flash test, no remapping.
  Ported from the ALIENTEK Apollo STM32F429 "实验40 NAND FLASH实验" example:
  initializes the FMC NAND (bank 3, 0x80000000), reads the device ID,
  identifies the chip, then in a loop:
    - erase block 2
    - write a deterministic pattern to its first page
    - read it back and verify every byte
  Results print over USART1 at 115200 baud; LED1 (PB0) blinks while idle.
************************************************/

#include <stdio.h>
#include <string.h>
#include "board.h"
#include "nand.h"

/* Use block 2 (past block 0/1) so we do not touch the boot/BOM area. */
#define TEST_BLOCK   2UL
#define TEST_PAGE    0U

static uint32_t golden = 0xA5F01234UL;

static void fill_pattern(uint8_t *buf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
    {
        buf[i] = (uint8_t)((i * 7U + 13U) ^ 0x5AU);
    }
}

static int verify_pattern(const uint8_t *buf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
    {
        if (buf[i] != (uint8_t)((i * 7U + 13U) ^ 0x5AU))
        {
            return (int)i;
        }
    }
    return -1;
}

int main(void)
{
    static uint8_t wbuf[NAND_MAX_PAGE_SIZE];
    static uint8_t rbuf[NAND_MAX_PAGE_SIZE];
    uint16_t page_size;
    uint32_t page_ix;
    uint8_t res;
    int mismatch;

    HAL_Init();
    Board_Init();

    printf("\r\n==== apollo-f429 NAND FLASH test ====\r\n");

    if (NAND_Init() != 0U)
    {
        printf("Unsupported NAND ID: 0x%08lX\r\n", (unsigned long)nand_dev.id);
        while (1)
        {
            LED1_TOGGLE();
            HAL_Delay(200);
        }
    }

    page_size = nand_dev.page_mainsize;
    page_ix   = TEST_BLOCK * nand_dev.block_pagenum + TEST_PAGE;
    printf("NAND ID : 0x%08lX\r\n", (unsigned long)nand_dev.id);
    printf("NAND    : %lu MB (%u B/page, %u page/blk, %u blk)\r\n",
           (unsigned long)((uint32_t)nand_dev.block_totalnum / 1024U)
               * (nand_dev.page_mainsize / 1024U)
               * nand_dev.block_pagenum,
           (unsigned)page_size, (unsigned)nand_dev.block_pagenum,
           (unsigned)nand_dev.block_totalnum);
    printf("Test    : erase block %lu, write/read page %lu (main area, "
           "non-ECC)\r\n",
           (unsigned long)TEST_BLOCK, (unsigned long)page_ix);

    fill_pattern(wbuf, page_size);

    while (1)
    {
        uint32_t start, erase_cycles, write_cycles, read_cycles;

        /* Erase the block. */
        start = DWT->CYCCNT;
        res = NAND_EraseBlock(TEST_BLOCK);
        erase_cycles = DWT->CYCCNT - start;
        if (res != 0U)
        {
            printf("ERASE FAIL (%u)\r\n", (unsigned)res);
        }
        else
        {
            printf("erase: %lu cycles\r\n", (unsigned long)erase_cycles);
        }

        /* Write one page. */
        start = DWT->CYCCNT;
        res = NAND_WritePage(page_ix, 0U, wbuf, page_size);
        write_cycles = DWT->CYCCNT - start;
        if (res != 0U)
        {
            printf("WRITE FAIL (%u)\r\n", (unsigned)res);
        }
        else
        {
            printf("write: %lu cycles (%lu.%03lu KB/s)\r\n",
                   (unsigned long)write_cycles,
                   (unsigned long)(page_size / 1024UL * SystemCoreClock / write_cycles),
                   (unsigned long)(((page_size / 1024UL * SystemCoreClock)
                                    % write_cycles) * 1000UL / write_cycles));
        }

        /* Read it back. */
        start = DWT->CYCCNT;
        res = NAND_ReadPage(page_ix, 0U, rbuf, page_size);
        read_cycles = DWT->CYCCNT - start;
        if (res != 0U)
        {
            printf("READ FAIL (%u)\r\n", (unsigned)res);
        }
        else
        {
            printf("read : %lu cycles (%lu.%03lu KB/s)\r\n",
                   (unsigned long)read_cycles,
                   (unsigned long)(page_size / 1024UL * SystemCoreClock / read_cycles),
                   (unsigned long)(((page_size / 1024UL * SystemCoreClock)
                                    % read_cycles) * 1000UL / read_cycles));
        }

        mismatch = -1;
        if (res == 0U)
        {
            mismatch = verify_pattern(rbuf, page_size);
        }
        printf("Result: %s%s\r\n", (res == 0U && mismatch < 0) ? "PASS" : "FAIL",
               (mismatch >= 0) ? " (data mismatch)" : "");
        printf("Golden: 0x%08lX\r\n", (unsigned long)golden);

        LED1_ON();
        HAL_Delay(800);
        LED1_OFF();
        HAL_Delay(3200);
    }

    return 0;
}