/**
  * @file    ov7670_dcmi_probe/src/main.c
  * @brief   Does the OV7670 emit pixels on the DCMI bus?
  *
  * Console-only. Minimal SCCB bring-up (only RST PG2 + PWDN PG3 + bit-bang
  * SCCB on PB6/PB7 + XCLK PA8 MCO1), then programs the ALIENTEK QVGA RGB565
  * table + COLOR BAR, then arms DCMI CONTINUOUS + CIRCULAR DMA ring and
  * watches:
  *   - DMA NDTR: does it move? (pixels flowing = clock+sync OK)
  *   - ring scan: color bars produce 0xF800/0x07E0/0x001F RGB565 words.
  *
  * The DCMI data pins are ONLY configured here (after SCCB is up) so they
  * don't disturb the sensor's SCCB (the bug found in the full app).
  */

#include "board.h"
#include <stdio.h>
#include "sccb_bitbang.h"
#include "ov7670_regs.h"

#define RST_GPIO_PORT   GPIOG
#define RST_GPIO_PIN    GPIO_PIN_2
#define PWDN_GPIO_PORT  GPIOG
#define PWDN_GPIO_PIN   GPIO_PIN_3

#define RING_SIZE   (48u * 1024u)
#define RING_WORDS  (RING_SIZE / 4u)
__attribute__((aligned(32))) uint8_t ring[RING_SIZE];

DCMI_HandleTypeDef hdcmi;
DMA_HandleTypeDef hdma;

/* ---------------- minimal SCCB bring-up (proven) -------------------- */
static void sccb_min_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOG_CLK_ENABLE();
    gpio.Pin = RST_GPIO_PIN | PWDN_GPIO_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOG, &gpio);

    /* RST low 20ms -> release, PWDN low (pwr on) */
    HAL_GPIO_WritePin(RST_GPIO_PORT, RST_GPIO_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PWDN_GPIO_PORT, PWDN_GPIO_PIN, GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(RST_GPIO_PORT, RST_GPIO_PIN, GPIO_PIN_SET);
    HAL_Delay(100);
}

/* ---------------- DCMI GPIO (data/sync) ---------------- */
static void dcmi_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_HIGH;
    gpio.Pull = GPIO_NOPULL;

    gpio.Alternate = GPIO_AF13_DCMI;
    gpio.Pin = GPIO_PIN_5;                HAL_GPIO_Init(GPIOI, &gpio);  /* VSYNC */
    gpio.Pin = GPIO_PIN_4;                HAL_GPIO_Init(GPIOA, &gpio);  /* HREF  */
    gpio.Pin = GPIO_PIN_6;                HAL_GPIO_Init(GPIOA, &gpio);  /* PCLK  */
    gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 |
               GPIO_PIN_14;               HAL_GPIO_Init(GPIOH, &gpio);  /* D0..D4 */
    gpio.Pin = GPIO_PIN_3;                HAL_GPIO_Init(GPIOD, &gpio);  /* D5 */
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;   HAL_GPIO_Init(GPIOI, &gpio);  /* D6,D7 */
}

/* Forward decls (defined below; score_colorbar uses them) */
static void start_ring_capture_ex(uint32_t vspol, uint32_t hspol, uint32_t pckpol);
static void start_ring_capture(uint32_t vspol, uint32_t hspol);

/* Score how "color-barry" the ring is. The OV7670 RGB565 color bar is
 * 8 vertical bars: 0xFFFF, 0xFFE0 (yellow), 0x07FF (cyan), 0x07E0 (green),
 * 0xF81F (magenta), 0xF800 (red), 0x001F (blue), 0x0000 (black). Try both
 * byte orders (0xABCD little-endian stored as CD AB - here we check the
 * 16-bit value both raw and byte-swapped since D0..D7 order may vary). */
static void score_colorbar(const char *label, uint32_t vspol, uint32_t hspol,
                           uint32_t pckpol)
{
    static const uint16_t bar[8] = {
        0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0x0000
    };
    uint32_t hits[8] = {0};
    uint32_t hit_swap[8] = {0};

    start_ring_capture_ex(vspol, hspol, pckpol);
    HAL_Delay(700);
    HAL_DCMI_Stop(&hdcmi);

    uint16_t cnt_any = 0;
    const uint32_t words = RING_SIZE / 2;
    for (uint32_t i = 0; i < words; i++)
    {
        uint16_t w = (uint16_t)(ring[i * 2] | ((uint16_t)ring[i * 2 + 1] << 8));
        uint16_t ws = (uint16_t)((w >> 8) | (w << 8));
        for (uint32_t b = 0; b < 8; b++)
        {
            if (w == bar[b]) { hits[b]++; cnt_any++; }
            if (ws == bar[b]) { hit_swap[b]++; }
        }
    }
    printf("  %s (pc=%s): ", label, pckpol == DCMI_PCKPOLARITY_RISING ? "R" : "F");
    for (uint32_t b = 0; b < 8; b++)
        printf("%s=%lu ", (b == 0 ? "W" : b == 1 ? "Yl" : b == 2 ? "Cy" :
                          b == 3 ? "Gn" : b == 4 ? "Mg" : b == 5 ? "Rd" :
                          b == 6 ? "Bl" : "Bk"),
               (unsigned long)hits[b]);
    printf(" | any=%lu swapped-any=%lu\r\n", (unsigned long)cnt_any,
           (unsigned long)(hit_swap[0]+hit_swap[1]+hit_swap[2]+hit_swap[3]+
                           hit_swap[4]+hit_swap[5]+hit_swap[6]+hit_swap[7]));
}

static void start_ring_capture_ex(uint32_t vspol, uint32_t hspol, uint32_t pckpol)
{
    HAL_DCMI_Stop(&hdcmi);
    HAL_DMA_Abort(&hdma);

    __HAL_RCC_DMA2_CLK_ENABLE();
    hdma.Instance = DMA2_Stream1;
    hdma.Init.Channel = DMA_CHANNEL_1;
    hdma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma.Init.MemInc = DMA_MINC_ENABLE;
    hdma.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma.Init.Mode = DMA_CIRCULAR;
    hdma.Init.Priority = DMA_PRIORITY_HIGH;
    hdma.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
    hdma.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
    hdma.Init.MemBurst = DMA_MBURST_INC4;
    hdma.Init.PeriphBurst = DMA_PBURST_SINGLE;
    __HAL_LINKDMA(&hdcmi, DMA_Handle, hdma);
    HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream1_IRQn);
    HAL_DMA_Init(&hdma);

    if (DCMI->RISR & DCMI_FLAG_OVRRI) __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_OVRRI);
    if (DCMI->RISR & DCMI_FLAG_ERRRI) __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_ERRRI);

    __HAL_RCC_DCMI_CLK_ENABLE();
    hdcmi.Instance = DCMI;
    hdcmi.Init.SynchroMode = DCMI_SYNCHRO_HARDWARE;
    hdcmi.Init.PCKPolarity = pckpol;
    hdcmi.Init.VSPolarity = vspol;
    hdcmi.Init.HSPolarity = hspol;
    hdcmi.Init.CaptureRate = DCMI_CR_ALL_FRAME;
    hdcmi.Init.ExtendedDataMode = DCMI_EXTEND_DATA_8B;
    HAL_DCMI_Init(&hdcmi);
    for (uint32_t i = 0; i < RING_SIZE; i++) ring[i] = 0xAA;   /* poison */
    HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_CONTINUOUS, (uint32_t)ring, RING_WORDS);
}

/* keep the old 2-arg wrapper for the polarity sweep */
static void start_ring_capture(uint32_t vspol, uint32_t hspol)
{
    start_ring_capture_ex(vspol, hspol, DCMI_PCKPOLARITY_RISING);
}

/* capture ring + wrappers (defined below) */
static void start_ring_capture_ex(uint32_t vspol, uint32_t hspol, uint32_t pckpol);
static void start_ring_capture(uint32_t vspol, uint32_t hspol);

/* Capture for a short window and count nonzero bytes. */

/* Capture for a short window and count nonzero bytes. */
static uint32_t capture_nonzero(uint32_t vspol, uint32_t hspol, const char *label)
{
    start_ring_capture(vspol, hspol);
    HAL_Delay(700);
    HAL_DCMI_Stop(&hdcmi);

    uint32_t nz = 0;
    for (uint32_t i = 0; i < RING_SIZE; i++)
    {
        if (ring[i] != 0xAA && ring[i] != 0x00) nz++;
    }
    printf("  %s: nonzero bytes = %lu / %lu (%.1f%%)\r\n", label,
           (unsigned long)nz, (unsigned long)RING_SIZE,
           100.0 * (double)nz / (double)RING_SIZE);

    /* For the promising combos, dump the ring start so we can identify the
     * data + count DISTINCT 16-bit words. A real color bar has ~8 distinct
     * values cycling every line; a static dark frame has 1-2. */
    if (nz > (RING_SIZE / 2))
    {
        /* Distinct 16-bit word count over the FIRST 16K words using a
         * compact 8 KB bitmap (65536 bits). */
        {
            static uint8_t bitmap[8192];
            for (uint32_t i = 0; i < 8192; i++) bitmap[i] = 0;
            for (uint32_t i = 0; i + 1 < 32768; i += 2)
            {
                uint16_t w = (uint16_t)(ring[i] | ((uint16_t)ring[i + 1] << 8));
                bitmap[w >> 3] |= (uint8_t)(1u << (w & 7u));
            }
            uint32_t dcount = 0;
            for (uint32_t i = 0; i < 8192; i++)
            {
                while (bitmap[i])
                {
                    dcount++;
                    bitmap[i] = (uint8_t)(bitmap[i] & (uint8_t)(bitmap[i] - 1));
                }
            }
            printf("    distinct 16-bit words (first 16K): %lu\r\n",
                   (unsigned long)dcount);
        }
        /* Dump the first ~2 lines assuming QVGA 320px RGB565 (640 B/line):
         * show words in rows of 40 so a vertical color-bar structure
         * (runs of identical words per bar) is obvious. */
        printf("    first line (w words, 40/row):\r\n");
        for (uint32_t r = 0; r < 8; r++)
        {
            printf("      ");
            for (uint32_t c = 0; c < 40; c++)
            {
                uint32_t i = (r * 40 + c) * 2;
                printf("%04x ", (unsigned)(ring[i] | ((uint16_t)ring[i + 1] << 8)));
            }
            printf("\r\n");
        }
    }
    return nz;
}

/* ---------------- scan the ring for RGB565 colors ------------------- */
static void scan_ring(void)
{
    uint32_t red = 0, green = 0, blue = 0, other = 0, total = 0;
    uint32_t nz = 0;

    /* dump the first 32 bytes: what did the DMA actually put in? */
    printf("  ring[0..31]: ");
    for (uint32_t i = 0; i < 32; i++) printf("%02x ", (unsigned)ring[i]);
    printf("\r\n");

    for (uint32_t i = 0; i + 1 < RING_SIZE; i += 2)
    {
        uint16_t w = (uint16_t)(ring[i] | ((uint16_t)ring[i + 1] << 8));
        if (w == 0xF800) red++;
        else if (w == 0x07E0) green++;
        else if (w == 0x001F) blue++;
        else if (w != 0x0000) other++;
        total++;
        if (w != 0x0000) nz++;
    }
    printf("  ring: total=%lu colorbars: R=%lu G=%lu B=%lu other=%lu nonzero=%lu (%.1f%%)\r\n",
           (unsigned long)total, (unsigned long)red, (unsigned long)green,
           (unsigned long)blue, (unsigned long)other, (unsigned long)nz,
           total ? 100.0 * (double)nz / (double)total : 0.0);
}

/* Dump the row structure at the ASSUMED 1280 B/row pitch: print the 16-bit
 * color at several columns for the first rows. If the bars are vertical,
 * the color at each fixed column is the same across rows; if the row pitch
 * is wrong, the column drifts and the colors change row-to-row. */
static void row_structure_dump(void)
{
    const uint8_t *b = ring;
    printf("row-structure dump (assumed 1280 B/row = 640 words):\r\n");
    printf("  row:  c0     c320   c400   c480   c560   c600   c630\r\n");
    for (uint32_t r = 0; r < 24; r++)
    {
        uint32_t base = r * 640u * 2u;            /* row r in bytes (assumed) */
        if (base + 1280u > RING_SIZE) break;
        printf("  %3lu:", (unsigned long)r);
        static const uint32_t cols[7] = { 0, 320, 400, 480, 560, 600, 630 };
        for (uint32_t k = 0; k < 7; k++)
        {
            uint16_t w = (uint16_t)(b[base + cols[k]*2u] |
                                    ((uint16_t)b[base + cols[k]*2u + 1u] << 8));
            printf(" %04x", (unsigned)w);
        }
        printf("\r\n");
    }
}

/* Measure the real row step (bytes) of the captured stream by
 * autocorrelation. A VGA 640px RGB565 row is 1280 B; but the OV7670 HREF
 * spans the full sensor window (~784 px/row), so the true pitch can be
 * ~1568 B when DCW/scaling are off. Search a WIDE range and also dump the
 * raw words so the bar periodicity is visible directly. */
static void measure_row_pitch(void)
{
    uint32_t best_p = 0, best_n = 0;
    printf("row-pitch autocorrelation (candidates 1200..1700 B):\r\n");
    for (uint32_t p = 1200; p <= 1700; p++)
    {
        uint32_t matches = 0, total = 0;
        for (uint32_t i = 0; (i + p) < RING_SIZE && i < 3000; i++)
        {
            uint8_t a = ring[i];
            if (a == 0xAA || a == 0x00) continue;
            total++;
            if (a == ring[i + p]) matches++;
        }
        if (total > 300 && matches > best_n)
        {
            best_n = matches;
            best_p = p;
        }
    }
    printf(">>> BEST row pitch = %lu B (%.1f px/row)\r\n",
           (unsigned long)best_p, (double)best_p / 2.0);
    printf("    640px RGB565 = 1280 B; 784px (full window) = 1568 B\n");

    /* Raw word dump: bar periodicity visible as repeated word runs. */
    printf("  ring[0..63] as words:\r\n    ");
    for (uint32_t i = 0; i < 64; i++)
    {
        printf("%04x ", (unsigned)(ring[i*2] | ((uint16_t)ring[i*2+1] << 8)));
        if ((i & 7) == 7) { printf("\r\n    "); }
    }
    printf("\r\n");

    /* row-structure at the best + at 1568 B */
    uint32_t pitches[2] = { best_p, 1568 };
    const char *names[2] = { "best", "1568" };
    for (uint32_t t = 0; t < 2; t++)
    {
        uint32_t pp = pitches[t] >> 1;            /* px per row */
        uint32_t step = pitches[t];
        printf("  row dump @ %s pitch (%lu B):\r\n    row: c0  c%d  c%d  c%d  c%d  c%d\n",
               names[t], (unsigned long)pp, (unsigned long)(pp*3/4),
               (unsigned long)(pp/2), (unsigned long)(pp/4), (unsigned long)(pp/8));
        for (uint32_t r = 0; r < 12; r++)
        {
            uint32_t base = r * step;
            if (base + step > RING_SIZE) break;
            printf("    %3lu:", (unsigned long)r);
            static const uint32_t frac[6] = {0, 1, 3, 4, 6, 7};  /* /8 */
            for (uint32_t k = 0; k < 6; k++)
            {
                uint32_t col = pp * frac[k] / 8;
                uint16_t w = (uint16_t)(ring[base + col*2] |
                                        ((uint16_t)ring[base + col*2 + 1] << 8));
                printf(" %04x", (unsigned)w);
            }
            printf("\r\n");
        }
    }
}

/* ---------------- GPIO activity test on the DCMI pins ---------------- */
/* Reconfigure the DCMI data/sync pins as plain INPUTS and watch for any
 * toggling. Decisive: if a pin never changes while the color bar runs, the
 * module's corresponding line is not driven (dead/not connected). */
static void gpio_activity_test(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint32_t i;

    printf("GPIO data-pin activity test (inputs, 200 x 1ms samples):\r\n");
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLDOWN;   /* pulled low: floating pins read 0 */
    gpio.Pin = GPIO_PIN_5;                      HAL_GPIO_Init(GPIOI, &gpio);  /* VSYNC */
    gpio.Pull = GPIO_PULLUP;                    /* pull up for sync pins */
    gpio.Pin = GPIO_PIN_4;                      HAL_GPIO_Init(GPIOA, &gpio);  /* HREF  */
    gpio.Pin = GPIO_PIN_6;                      HAL_GPIO_Init(GPIOA, &gpio);  /* PCLK  */
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 |
               GPIO_PIN_14;                     HAL_GPIO_Init(GPIOH, &gpio);  /* D0..D4 */
    gpio.Pin = GPIO_PIN_3;                      HAL_GPIO_Init(GPIOD, &gpio);  /* D5 */
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;         HAL_GPIO_Init(GPIOI, &gpio);  /* D6,D7 */

    /* per-pin toggle counters (11 pins: VSYNC, HREF, PCLK, D0..D7) */
    uint32_t togg[11] = {0};
    uint32_t last[11];
    GPIO_TypeDef *ports[11];
    uint16_t pins[11];
    ports[0] = GPIOI; pins[0] = GPIO_PIN_5;                       /* VSYNC */
    ports[1] = GPIOA; pins[1] = GPIO_PIN_4;                       /* HREF  */
    ports[2] = GPIOA; pins[2] = GPIO_PIN_6;                       /* PCLK  */
    ports[3] = GPIOH; pins[3] = GPIO_PIN_9;                       /* D0 */
    ports[4] = GPIOH; pins[4] = GPIO_PIN_10;                      /* D1 */
    ports[5] = GPIOH; pins[5] = GPIO_PIN_11;                      /* D2 */
    ports[6] = GPIOH; pins[6] = GPIO_PIN_12;                      /* D3 */
    ports[7] = GPIOH; pins[7] = GPIO_PIN_14;                      /* D4 */
    ports[8] = GPIOD; pins[8] = GPIO_PIN_3;                       /* D5 */
    ports[9] = GPIOI; pins[9] = GPIO_PIN_6;                       /* D6 */
    ports[10] = GPIOI; pins[10] = GPIO_PIN_7;                     /* D7 */

    for (int k = 0; k < 11; k++) last[k] = HAL_GPIO_ReadPin(ports[k], pins[k]);

    for (i = 0; i < 200; i++)
    {
        for (int k = 0; k < 11; k++)
        {
            uint32_t v = HAL_GPIO_ReadPin(ports[k], pins[k]);
            if (v != last[k]) togg[k]++;
            last[k] = v;
        }
        HAL_Delay(1);
    }

    printf("  toggles: VSYNC(PI5)=%lu HREF(PA4)=%lu PCLK(PA6)=%lu "
           "D0(PH9)=%lu D1(PH10)=%lu D2(PH11)=%lu D3(PH12)=%lu "
           "D4(PH14)=%lu D5(PD3)=%lu D6(PI6)=%lu D7(PI7)=%lu\r\n",
           (unsigned long)togg[0], (unsigned long)togg[1],
           (unsigned long)togg[2], (unsigned long)togg[3],
           (unsigned long)togg[4], (unsigned long)togg[5],
           (unsigned long)togg[6], (unsigned long)togg[7],
           (unsigned long)togg[8], (unsigned long)togg[9],
           (unsigned long)togg[10]);
    printf("  (a toggle count near 0 = pin never changes = not driven)\r\n");
}

int main(void)
{
    HAL_Init();
    Board_Init();
    printf("\r\n=== ov7670_dcmi_probe (does the sensor emit pixels?) ===\r\n");

    /* XCLK = PA8 MCO1 = HSE/2 = 12.5 MHz (proven). */
    HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_HSE, RCC_MCODIV_2);
    printf("XCLK 12.5 MHz on PA8 (MCO1)\r\n");

    /* minimal SCCB bring-up + config + color bar */
    sccb_min_init();
    SCCB_BB_InitGPIO();
    printf("SCCB minimal init done\r\n");

    uint8_t pid = SCCB_BB_ReadReg(0x0A);
    printf("PID=0x%02x\r\n", (unsigned)pid);
    if (pid != 0x76)
    {
        printf("sensor not detected - aborting\r\n");
        while (1) { }
    }

    printf("applying QVGA RGB565 table... ");
    printf("%s\r\n", OV7670_ApplyConfig() == 0 ? "OK" : "FAIL");
    printf("  readback: COM7=0x%02x COM15=0x%02x "
           "SC_XSC=0x%02x SC_YSC=0x%02x\r\n",
           (unsigned)SCCB_BB_ReadReg(0x12),
           (unsigned)SCCB_BB_ReadReg(0x40),
           (unsigned)SCCB_BB_ReadReg(0x70),
           (unsigned)SCCB_BB_ReadReg(0x71));
    OV7670_ColorBarOn();
    printf("color bar ON (0x70=0x%02x 0x71=0x%02x)\r\n",
           (unsigned)SCCB_BB_ReadReg(0x70),
           (unsigned)SCCB_BB_ReadReg(0x71));

    /* GPIO activity test FIRST (before DCMI AF) - prove the module drives
     * its sync/data pins at all. */
    gpio_activity_test();

    /* DCMI data pins only now (after SCCB). */
    dcmi_gpio_init();

    /* A/B: does the OpenMV RGB565 tweak (COM15=0xD0 full range + CLKRC
     * rewrite) fix the format? The ALIENTEK table sets COM15=0x10 (RGB565
     * limited range 10..F0) which is dark/washed. Score the color bar in
     * both configs, both byte orders, both PCLK edges. */
    printf("phase A: ALIENTEK table (COM15=0x10, CLKRC=0x00):\r\n");
    score_colorbar("ALI pc=R", DCMI_VSPOLARITY_HIGH, DCMI_HSPOLARITY_LOW,
                   DCMI_PCKPOLARITY_RISING);
    score_colorbar("ALI pc=F", DCMI_VSPOLARITY_HIGH, DCMI_HSPOLARITY_LOW,
                   DCMI_PCKPOLARITY_FALLING);

    printf("phase B: + OpenMV tweak (COM15=0xD0, CLKRC rewrite):\r\n");
    OV7670_OpenMvRgbTweak();
    printf("  readback: COM15=0x%02x CLKRC=0x%02x\r\n",
           (unsigned)SCCB_BB_ReadReg(0x40),
           (unsigned)SCCB_BB_ReadReg(0x11));
    score_colorbar("OMV pc=R", DCMI_VSPOLARITY_HIGH, DCMI_HSPOLARITY_LOW,
                   DCMI_PCKPOLARITY_RISING);
    score_colorbar("OMV pc=F", DCMI_VSPOLARITY_HIGH, DCMI_HSPOLARITY_LOW,
                   DCMI_PCKPOLARITY_FALLING);

    /* Keep the proven polarity for the summary dump + row-pitch measurement. */
    start_ring_capture(DCMI_VSPOLARITY_HIGH, DCMI_HSPOLARITY_LOW);

    printf("capturing 3 s (continuous circular ring)...\r\n");
    uint16_t ndtr_prev = RING_WORDS;
    for (int i = 0; i < 6; i++)
    {
        HAL_Delay(500);
        uint16_t ndtr = DMA2_Stream1->NDTR & 0xFFFFu;
        printf("  t=%d.%d NDTR=0x%04x (d=%-6d) moved=%s DCMI_RISR=0x%08lx\r\n",
               i / 2, (i % 2) * 5, (unsigned)ndtr,
               (int)((int16_t)ndtr_prev - (int16_t)ndtr),
               (ndtr != ndtr_prev) ? "YES" : "no",
               (unsigned long)DCMI->RISR);
        ndtr_prev = ndtr;
    }
    HAL_DCMI_Stop(&hdcmi);
    HAL_DMA_Abort(&hdma);

    measure_row_pitch();
    scan_ring();

    printf("--- done ---\r\n");
    while (1) { }
}

/* IRQ handlers: DMA TC keeps the HAL happy + DCMI frame events. */
void DMA2_Stream1_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma); }
void DCMI_IRQHandler(void)          { HAL_DCMI_IRQHandler(&hdcmi); }
void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *h) { (void)h; }