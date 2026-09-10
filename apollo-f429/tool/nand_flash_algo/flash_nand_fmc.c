/* flash_nand_fmc.c - on-board NAND (MT29F4G08ABADA) flash algorithm for
 * STM32F429IGT6 (apollo-f429) using the FMC NAND peripheral (bank 3).
 *
 * Ported from the ALIENTEK Apollo "实验40 NAND FLASH实验" example
 * (FMC bank-3 NAND low-level driver) and the h723-mini OCTOSPI flash algorithm
 * pattern (tool/qspi_map/algo/flash_w25q64_ospi.c).
 *
 * Functional model:
 *   - The stage-2 app is linked at 0xC0000000 (SDRAM bank 1 window). The NAND
 *     acts as its storage: address 0xC0000000 + off maps to NAND byte offset
 *     `off` (page = off / 2048, column = off % 2048).
 *   - Non-ECC path (main area only), exactly like nand_test.c.
 *   - Sector = one NAND block (64 pages * 2048 B = 128 KiB), page = 2048 B.
 *
 * Runs ON the target CPU (probe-rs loads it into internal SRAM). Position
 * independent: no globals, only register access via literal pools.
 */

typedef volatile unsigned long  vu32;
typedef volatile unsigned char  vu8;

/* ---- clocks (RCC base 0x40023800) ---- */
#define RCC_AHB1ENR  (0x40023800UL + 0x30UL)   /* GPIO clocks: D=bit3,E=bit4,G=bit6 */
#define RCC_AHB3ENR  (0x40023800UL + 0x38UL)   /* FMC = bit0 */

/* ---- GPIO (AHB1) ---- */
#define GPIOD_BASE   0x40020C00UL
#define GPIOE_BASE   0x40021000UL
#define GPIOG_BASE   0x40021800UL
#define GPIO_MODER   0x00UL
#define GPIO_OTYPER  0x04UL
#define GPIO_OSPEEDR 0x08UL
#define GPIO_PUPDR   0x0CUL
#define GPIO_IDR     0x10UL
#define GPIO_ODR     0x14UL
#define GPIO_AFRL    0x20UL
#define GPIO_AFRH    0x24UL

/* ---- FMC NAND bank 3 (FMC_Bank2_3 = 0xA0000060) ---- */
#define FMC_B2_3     0xA0000060UL
#define FMC_PCR3     (FMC_B2_3 + 0x20UL)   /* 0xA0000080 */
#define FMC_SR3      (FMC_B2_3 + 0x24UL)   /* 0xA0000084 */
#define FMC_PMEM3    (FMC_B2_3 + 0x28UL)   /* 0xA0000088 */
#define FMC_PATT3    (FMC_B2_3 + 0x2CUL)   /* 0xA000008C */
#define FMC_ECCR3    (FMC_B2_3 + 0x34UL)   /* 0xA0000094 */

/* NAND window (base 0x80000000): CLE/ALE are bit16/bit17 of the byte address. */
#define NAND_CMD_    0x80010000UL   /* base | (1<<16) -> CLE */
#define NAND_ALE     0x80020000UL   /* base | (1<<17) -> ALE */
#define NAND_DATA    0x80000000UL

/* ---- NAND commands (main-area, non-ECC) ---- */
#define CMD_READID   0x90U
#define CMD_RESET    0xFFU
#define CMD_STATUS   0x70U
#define CMD_READ0    0x00U
#define CMD_READ30   0x30U
#define CMD_WRITE    0x80U
#define CMD_PROG     0x10U
#define CMD_ERASE0   0x60U
#define CMD_ERASE1   0xD0U
#define STA_READY    0x40U

/* NAND geometry (MT29F4G08ABADA: 2048 B/page, 64 page/blk -> 128 KiB blk). */
#define NAND_PAGE_SIZE    2048UL
#define NAND_PAGES_PER_BLK 64UL
#define NAND_BLOCK_SIZE   (NAND_PAGE_SIZE * NAND_PAGES_PER_BLK)  /* 128 KiB */
#define NAND_TOTAL_BLOCKS 4096UL

/* Logical flash window the app image lives in (SDRAM bank1 base). */
#define APP_BASE      0xC0000000UL

/* ------------------------------------------------------------------------ */
static void fmc_hw_init(void)
{
    unsigned long pcr;

    /* 1. Peripheral + GPIO clocks (dummy read back, as the HAL macros do). */
    *(vu32 *)RCC_AHB1ENR |= (0x1UL << 3) | (0x1UL << 4) | (0x1UL << 6); /* D/E/G */
    (void)*(vu32 *)RCC_AHB1ENR;
    *(vu32 *)RCC_AHB3ENR |= 0x1UL;                                       /* FMC */
    (void)*(vu32 *)RCC_AHB3ENR;

    /* 2. GPIO - FMC NAND bank 3 (same pins as bare/nand_test/src/nand.c):
     *      PD6        = R/B (input, pull-up)
     *      PG9        = NCE3 (AF12)
     *      PD0/1/4/5/11/12/14/15, PE7/8/9/10 = FMC data (AF12)
     *   AF value = 0xC, MODER = 2'b10, OSPEEDR HIGH = 2'b10. */

    /* PD6 input pull-up: MODER[13:12]=00, PUPDR[13:12]=01. */
    *(vu32 *)(GPIOD_BASE + GPIO_MODER) &= ~(0x3UL << 12);
    *(vu32 *)(GPIOD_BASE + GPIO_PUPDR)  &= ~(0x3UL << 12);
    *(vu32 *)(GPIOD_BASE + GPIO_PUPDR)  |=  (0x1UL << 12);

    /* PD AF pins.  AFRL nibbles: PD0=[3:0], PD1=[7:4], PD4=[19:16], PD5=[23:20].
     * AFRH nibbles: PD11=[15:12], PD12=[19:16], PD14=[27:24], PD15=[31:28]. */
    {
        unsigned long afrl = *(vu32 *)(GPIOD_BASE + GPIO_AFRL);
        afrl = (afrl & ~0x000000FFUL) | (0xC << 0) | (0xC << 4);      /* PD0|PD1 */
        afrl = (afrl & ~0xFF000000UL) | (0xC << 16) | (0xC << 20);    /* PD4|PD5 */
        *(vu32 *)(GPIOD_BASE + GPIO_AFRL) = afrl;
    }
    {
        unsigned long afrh = *(vu32 *)(GPIOD_BASE + GPIO_AFRH);
        afrh = (afrh & ~0x00FFF000UL)
             | (0xC << 12)                                             /* PD11 */
             | (0xC << 16)                                             /* PD12 */
             | (0xC << 24)                                             /* PD14 */
             | (0xC << 28);                                            /* PD15 */
        *(vu32 *)(GPIOD_BASE + GPIO_AFRH) = afrh;
    }

    /* PE7 = AFRL nibble 7 (bits 31:28).  PE8/PE9/PE10 = AFRH nibbles 0/1/2. */
    {
        unsigned long afrl = *(vu32 *)(GPIOE_BASE + GPIO_AFRL);
        afrl = (afrl & ~0xF0000000UL) | (0xC << 28);    /* PE7 */
        *(vu32 *)(GPIOE_BASE + GPIO_AFRL) = afrl;
    }
    {
        unsigned long afrh = *(vu32 *)(GPIOE_BASE + GPIO_AFRH);
        afrh = (afrh & ~0x00000FFFUL)
             | (0xC <<  0)                                            /* PE8  */
             | (0xC <<  4)                                            /* PE9  */
             | (0xC <<  8);                                           /* PE10 */
        *(vu32 *)(GPIOE_BASE + GPIO_AFRH) = afrh;
    }

    /* PG9 = AFRH nibble 1 (bits 11:8). */
    {
        unsigned long afrh = *(vu32 *)(GPIOG_BASE + GPIO_AFRH);
        afrh = (afrh & ~0x00000F00UL) | (0xC << 8);
        *(vu32 *)(GPIOG_BASE + GPIO_AFRH) = afrh;
    }

    /* MODER -> AF (2'b10) + OSPEEDR -> HIGH (2'b10) for data/NCE pins. */
    {
        unsigned long moder = *(vu32 *)(GPIOD_BASE + GPIO_MODER);
        moder |= (0x2UL << 0) | (0x2UL << 2)   /* PD0, PD1   */
               | (0x2UL << 8)  | (0x2UL << 10) /* PD4, PD5   */
               | (0x2UL << 22) | (0x2UL << 24) /* PD11, PD12 */
               | (0x2UL << 28) | (0x2UL << 30);/* PD14, PD15 */
        *(vu32 *)(GPIOD_BASE + GPIO_MODER) = moder;
        *(vu32 *)(GPIOD_BASE + GPIO_OSPEEDR) |= (0x2UL << 0) | (0x2UL << 2)
               | (0x2UL << 8) | (0x2UL << 10)
               | (0x2UL << 22) | (0x2UL << 24) | (0x2UL << 28) | (0x2UL << 30);
    }
    {
        unsigned long moder = *(vu32 *)(GPIOE_BASE + GPIO_MODER);
        moder |= (0x2UL << 14) | (0x2UL << 16)   /* PE7, PE8 */
               | (0x2UL << 18) | (0x2UL << 20);  /* PE9, PE10 */
        *(vu32 *)(GPIOE_BASE + GPIO_MODER) = moder;
        *(vu32 *)(GPIOE_BASE + GPIO_OSPEEDR) |= (0x2UL << 14) | (0x2UL << 16)
               | (0x2UL << 18) | (0x2UL << 20);
    }
    {
        unsigned long moder = *(vu32 *)(GPIOG_BASE + GPIO_MODER);
        moder |= (0x2UL << 18);                  /* PG9 */
        *(vu32 *)(GPIOG_BASE + GPIO_MODER) = moder;
        *(vu32 *)(GPIOG_BASE + GPIO_OSPEEDR) |= (0x2UL << 18);
    }

    /* 3. FMC NAND bank3 timing (same as nand_test HAL config:
     *    Setup=2, Wait=3, Hold=2, HiZ=1). */
    *(vu32 *)FMC_PMEM3 = (2UL) | (3UL << 8) | (2UL << 16) | (1UL << 24);
    *(vu32 *)FMC_PATT3 = (2UL) | (3UL << 8) | (2UL << 16) | (1UL << 24);

    /* 4. PCR3: 8-bit, ECC off, TCLR=0, TAR=1, bank enable. */
    pcr  = (0UL << 1)   /* PWAITEN = 0            */
         | (1UL << 2)   /* PBKEN   = 1 (enable)   */
         | (1UL << 3)   /* PTYP NAND type         */
         | (0UL << 4)   /* PWID: 8-bit            */
         | (0UL << 6)   /* ECCEN = 0              */
         | (0UL << 9)   /* TCLR = 0               */
         | (1UL << 13); /* TAR = 1                */
    *(vu32 *)FMC_PCR3 = pcr;
    (void)FMC_SR3;
    (void)FMC_ECCR3;
}

/* ------------------------------------------------------------------------ */
static unsigned char nand_status(void)
{
    unsigned long d;

    *(vu8 *)NAND_CMD_ = CMD_STATUS;
    for (d = 0; d < 100; d++) { }
    return *(vu8 *)NAND_DATA;
}

static int nand_ready(void)
{
    unsigned long t = 0x1FFFFFUL;
    while (t--)
    {
        if (nand_status() & STA_READY) { return 0; }
    }
    return 1;
}

static void nand_delay(volatile unsigned long i)
{
    while (i > 0) { i--; }
}

static unsigned long nand_read_id(void)
{
    unsigned char b[5];
    unsigned long i;

    *(vu8 *)NAND_CMD_ = CMD_READID;
    *(vu8 *)NAND_ALE  = 0x00U;
    for (i = 0; i < 5; i++) { b[i] = *(vu8 *)NAND_DATA; }
    return ((unsigned long)b[1] << 24) | ((unsigned long)b[2] << 16)
         | ((unsigned long)b[3] << 8) | b[4];
}

static int nand_reset(void)
{
    *(vu8 *)NAND_CMD_ = CMD_RESET;
    return nand_ready();
}

/* Erase NAND block `blk`. Returns 0 on success. */
static int nand_erase_block(unsigned long blk)
{
    unsigned long row = blk * NAND_PAGES_PER_BLK;

    if (blk >= NAND_TOTAL_BLOCKS) { return 1; }
    *(vu8 *)NAND_CMD_ = CMD_ERASE0;
    *(vu8 *)NAND_ALE  = (unsigned char)(row & 0xFF);
    *(vu8 *)NAND_ALE  = (unsigned char)((row >> 8) & 0xFF);
    *(vu8 *)NAND_ALE  = (unsigned char)((row >> 16) & 0xFF);
    *(vu8 *)NAND_CMD_ = CMD_ERASE1;
    return nand_ready();
}

/* Program `n` bytes to NAND page `page`, column `col` (n <= 2048-col). */
static int nand_prog_page(unsigned long page, unsigned long col,
                          const unsigned char *buf, unsigned long n)
{
    unsigned long i;
    if (col + n > NAND_PAGE_SIZE) { return 1; }

    *(vu8 *)NAND_CMD_ = CMD_WRITE;
    *(vu8 *)NAND_ALE  = (unsigned char)(col & 0xFF);
    *(vu8 *)NAND_ALE  = (unsigned char)((col >> 8) & 0xFF);
    *(vu8 *)NAND_ALE  = (unsigned char)(page & 0xFF);
    *(vu8 *)NAND_ALE  = (unsigned char)((page >> 8) & 0xFF);
    *(vu8 *)NAND_ALE  = (unsigned char)((page >> 16) & 0xFF);
    for (i = 0; i < n; i++) { *(vu8 *)NAND_DATA = buf[i]; }
    *(vu8 *)NAND_CMD_ = CMD_PROG;
    return nand_ready();
}

/* Read `n` bytes from NAND page `page`, column `col`. */
static int nand_read_page(unsigned long page, unsigned long col,
                          unsigned char *buf, unsigned long n)
{
    unsigned long i;
    if (col + n > NAND_PAGE_SIZE) { return 1; }

    *(vu8 *)NAND_CMD_ = CMD_READ0;
    *(vu8 *)NAND_ALE  = (unsigned char)(col & 0xFF);
    *(vu8 *)NAND_ALE  = (unsigned char)((col >> 8) & 0xFF);
    *(vu8 *)NAND_ALE  = (unsigned char)(page & 0xFF);
    *(vu8 *)NAND_ALE  = (unsigned char)((page >> 8) & 0xFF);
    *(vu8 *)NAND_ALE  = (unsigned char)((page >> 16) & 0xFF);
    *(vu8 *)NAND_CMD_ = CMD_READ30;
    if (nand_ready()) { return 1; }
    for (i = 0; i < n; i++) { buf[i] = *(vu8 *)NAND_DATA; }
    return 0;
}

/* ------------------------------------------------------------------------ */

int Init(unsigned long adr, unsigned long clk, unsigned long fnc)
{
    unsigned long id;
    (void)adr; (void)clk; (void)fnc;

    fmc_hw_init();
    if (nand_reset()) { return 1; }
    id = nand_read_id();

    /* MT29F4G08ABADA 0xDC909556, MT29F16G08ABABA 0x48002689. */
    return (id == 0xDC909556UL || id == 0x48002689UL) ? 0 : 1;
}

int UnInit(unsigned long fnc)
{
    (void)fnc;
    return 0;
}

int EraseSector(unsigned long adr)
{
    unsigned long off = adr - APP_BASE;
    unsigned long blk = off / NAND_BLOCK_SIZE;
    int attempt;
    for (attempt = 0; attempt < 3; attempt++)
    {
        if (nand_erase_block(blk)) { continue; }
        return 0;
    }
    return 1;
}

int EraseChip(void)
{
    unsigned long blk;
    for (blk = 0; blk < NAND_TOTAL_BLOCKS; blk++)
    {
        if (nand_erase_block(blk)) { return 1; }
    }
    return 0;
}

int ProgramPage(unsigned long adr, unsigned long sz, unsigned char *buf)
{
    unsigned long off = adr - APP_BASE;
    while (sz > 0)
    {
        unsigned long page = off / NAND_PAGE_SIZE;
        unsigned long col  = off % NAND_PAGE_SIZE;
        unsigned long n    = NAND_PAGE_SIZE - col;
        int attempt;

        if (n > sz) { n = sz; }
        for (attempt = 0; attempt < 4; attempt++)
        {
            if (nand_prog_page(page, col, buf, n)) { continue; }
            break;
        }
        if (attempt == 4) { return 1; }

        off += n;
        buf += n;
        sz  -= n;
    }
    return 0;
}

int Verify(unsigned long adr, unsigned long sz, unsigned char *buf)
{
    unsigned long off = adr - APP_BASE;
    unsigned long done = 0;

    while (done < sz)
    {
        unsigned long page = (off + done) / NAND_PAGE_SIZE;
        unsigned long col  = (off + done) % NAND_PAGE_SIZE;
        unsigned long room = NAND_PAGE_SIZE - col;
        unsigned long step = sz - done;
        unsigned char tmp[16];
        unsigned long i;

        if (step > room) { step = room; }
        while (step > 0)
        {
            unsigned long c = (step > 16UL) ? 16UL : step;
            if (nand_read_page(page, col, tmp, c)) { return 1; }
            for (i = 0; i < c; i++)
            {
                if (tmp[i] != buf[done + i]) { return 1; }
            }
            done += c;
            col  += c;
            step -= c;
        }
    }
    return 0;
}