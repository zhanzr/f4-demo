/**
  * @file    main.c
  * @brief   Stage-1 bootloader for the apollo-f429 two-stage NAND boot.
  *
  * Boots from internal flash, brings up the 180 MHz clock tree, console, LEDs,
  * SDRAM (W9825G6KH bank 1 @ 0xC0000000) and the on-board NAND
  * (MT29F4G08ABADA bank 3). It then loads the stage-2 firmware image stored at
  * NAND byte offset 0 (the app/ `algo/` probe-rs flash algorithm writes it
  * there), copies it into SDRAM at 0xC0000000 and performs a *basic*
  * bootability check:
  *   - the initial stack pointer (word 0) must land in the SDRAM window, and
  *   - the reset vector (word 1) must be a thumb pointer inside that window.
  * If the check passes it sets VTOR, jumps to the app (which now runs entirely
  * from SDRAM). If it fails it loops: toggles the LED and prints the check
  * result, so a missing/bad NAND image is visible on both LED and console
  * without a debugger.
  */

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "board.h"
#include "uart_printf.h"
#include "nand.h"

#define APP_BASE       0xC0000000UL
#define APP_LIMIT      0xC0000000UL + 0x2000000UL   /* 32 MiB SDRAM window */
#define APP_IMAGE_MAX  0x00100000UL                 /* hard safety cap: 1 MiB */
#define APP_PAGES      (APP_IMAGE_MAX / 2048UL)
/* Consecutive fully-erased (0xFF) NAND pages that mark the end of the image.
 * The app image is stored *raw* (no length header), so the bootloader finds
 * its end by scanning for erased pages - NAND blocks are 0xFF after erase,
 * and any pages past the written image read back all-0xFF. A 4-page (8 KiB)
 * run inside real code/data is essentially impossible, while the erased tail
 * is far longer, so this is a reliable end-of-image marker. */
#define APP_END_RUN    4U

/* The app's reset-vector SP may sit in internal SRAM (stack kept there) or in
 * the SDRAM window; the reset vector itself must be in the SDRAM code space.
 * SP may equal the SRAM top (the stack starts there and grows down). */
#define SP_SRAM_BASE   0x20000000UL
#define SP_SRAM_LIMIT  0x20000000UL + 0x30000UL     /* 192 KiB SRAM */
#define SRAM_VECTOR_OK(sp) \
    ((sp) >= SP_SRAM_BASE && (sp) <= SP_SRAM_LIMIT) || \
    ((sp) >= APP_BASE && (sp) < APP_LIMIT)

typedef void (*pfnVoid)(void);

extern void Board_SDRAM_EarlyInit(void);

/* On the F4, SDRAM bank 1 (0xC0000000) falls in the ARMv7-M default
 * External-device (XN) region, so the CPU faults on the first fetch from the
 * app copy. Mark the whole 32 MiB SDRAM window as Normal cacheable +
 * executable before jumping (mirrors h723's mpu_ospi_config()). */
static void mpu_sdram_config(void)
{
    MPU_Region_InitTypeDef m = {0};

    HAL_MPU_Disable();

    m.Enable           = MPU_REGION_ENABLE;
    m.Number           = MPU_REGION_NUMBER0;
    m.BaseAddress      = 0xC0000000u;
    m.Size             = MPU_REGION_SIZE_32MB;
    m.SubRegionDisable = 0;
    m.TypeExtField     = MPU_TEX_LEVEL1;              /* Normal write-back */
    m.AccessPermission = MPU_REGION_FULL_ACCESS;
    m.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
    m.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
    m.IsCacheable      = MPU_ACCESS_CACHEABLE;
    m.IsBufferable     = MPU_ACCESS_BUFFERABLE;
    HAL_MPU_ConfigRegion(&m);

    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

/* ------------------------------------------------------------------------ */
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Copy the app image from NAND bank3 offset 0 into SDRAM at APP_BASE.
 * Stops at the first run of APP_END_RUN consecutive erased (0xFF) pages,
 * so any app size up to APP_IMAGE_MAX works without recompiling the
 * bootloader. Sets *out_bytes to the number of bytes copied. */
static int load_app(uint8_t *dst, uint32_t *out_bytes)
{
    uint32_t page;
    uint32_t ff_run = 0;
    uint32_t copied = 0;

    for (page = 0; page < APP_PAGES; page++)
    {
        uint8_t res = NAND_ReadPage(page, 0U, dst + page * 2048UL, 2048U);
        if (res != 0U)
        {
            printf("  NAND read page %lu FAIL (%u)\r\n",
                   (unsigned long)page, (unsigned)res);
            return 1;
        }

        /* Check whether this whole page is erased (all 0xFF). */
        {
            const uint8_t *p = dst + page * 2048UL;
            uint32_t i, is_ff = 1;
            for (i = 0; i < 2048UL; i++)
            {
                if (p[i] != 0xFFU) { is_ff = 0; break; }
            }
            if (is_ff)
            {
                if (++ff_run >= APP_END_RUN)
                {
                    break;   /* found the erased tail: image ends here */
                }
            }
            else
            {
                ff_run = 0;
                copied = (page + 1U) * 2048UL;
            }
        }
    }

    *out_bytes = copied;
    return 0;
}

/* Basic bootability check on the firmware copied to SDRAM. The reset vector
 * must lie inside the bytes actually copied (img_bytes), so a truncated copy
 * (erased tail misdetected as image) fails here rather than running garbage. */
static int app_bootable(uint32_t img_bytes, uint32_t *out_sp, uint32_t *out_rv)
{
    uint8_t hdr[8];
    uint32_t sp, rv;

    memcpy(hdr, (const void *)APP_BASE, sizeof hdr);
    sp = rd32(&hdr[0]);
    rv = rd32(&hdr[4]);
    *out_sp = sp;
    *out_rv = rv;

    return (SRAM_VECTOR_OK(sp)) &&
           (rv & 1u) && (rv & ~1u) >= APP_BASE &&
           (rv & ~1u) < APP_BASE + img_bytes;
}

/* ------------------------------------------------------------------------ */
static void jump_to_app(void)
{
    uint32_t sp    = *(volatile uint32_t *)APP_BASE;
    uint32_t reset = *(volatile uint32_t *)(APP_BASE + 4u);

    __disable_irq();
    SCB->VTOR = APP_BASE;               /* relocate vector table to SDRAM */
    __set_MSP(sp);
    ((pfnVoid)reset)();                 /* app reset handler (thumb)     */
    while (1) { }                       /* never reached                 */
}

/* ------------------------------------------------------------------------ */
int main(void)
{
    uint32_t sp = 0, rv = 0;
    uint32_t img_bytes = 0;
    int ok;

    HAL_Init();
    Board_Init();
    Board_SDRAM_EarlyInit();

    printf("\r\n=== apollo-f429 NAND stage-1 boot @ %lu Hz ===\r\n",
           (unsigned long)SystemCoreClock);
    printf("SDRAM W9825G6KH bank1 @ 0x%08lX\r\n", (unsigned long)APP_BASE);

    if (NAND_Init() != 0U)
    {
        printf("NAND init FAIL (ID 0x%08lX)\r\n", (unsigned long)nand_dev.id);
        ok = 0;
    }
    else
    {
        printf("NAND %lu MB, ID 0x%08lX - loading app ...\r\n",
               (unsigned long)((uint32_t)nand_dev.block_totalnum / 1024U)
                   * (nand_dev.page_mainsize / 1024U) * nand_dev.block_pagenum,
               (unsigned long)nand_dev.id);
        ok = (load_app((uint8_t *)APP_BASE, &img_bytes) == 0);
        if (ok)
        {
            printf("  app image: %lu bytes (%lu pages)\r\n",
                   (unsigned long)img_bytes,
                   (unsigned long)(img_bytes / nand_dev.page_mainsize));
        }
        ok = ok && app_bootable(img_bytes, &sp, &rv);
    }

    printf("NAND firmware check: %s (SP=0x%08lX, Reset=0x%08lX)\r\n",
           ok ? "PASS - booting" : "FAIL",
           (unsigned long)sp, (unsigned long)rv);

    if (ok)
    {
        printf("jumping to 0x%08lX ...\r\n", (unsigned long)APP_BASE);
        mpu_sdram_config();
        jump_to_app();
        return 0;
    }

    /* No bootable firmware: loop, toggle the LED, print the result. */
    printf("no bootable firmware on NAND - waiting (LED1 blinks)\r\n");
    while (1)
    {
        LED1_ON();
        HAL_Delay(300);
        LED1_OFF();
        HAL_Delay(300);
    }
}