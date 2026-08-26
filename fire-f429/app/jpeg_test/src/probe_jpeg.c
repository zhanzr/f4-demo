/**
  * @file    probe_jpeg.c
  * @brief   JPEG-output probe for the fire-f429 OV5640 ("FD5640") module.
  *
  * Empirically checks whether the sensor can emit a valid JPEG stream on the
  * parallel 8-bit DVP bus. The module is silk-screened "FD5640 500W-V1 1" and
  * the esp32-camera issue tracker (#307, #368) shows fake OV5640s failing to
  * produce valid JPEG (cam_hal: NO-SOI) while RGB565 still works.
  *
  * This probe (all console output, no display):
  *   1. Reads PID (0x300A/B) + OmniVision manufacturer ID (0x300C/D).
  *   2. DVP sanity: vendor QVGA RGB565 + color bar, SNAPSHOT into snap_buf
  *      (the proven lcd-app path: frame size == DMA buffer size).
  *   3. JPEG phases: QVGA geometry + JPEG-enable register lists, then
  *      CONTINUOUS DCMI + CIRCULAR DMA ring into jpeg_cap_buf; every
  *      captured buffer is scanned for real JPEG markers (SOI FF D8, EOI
  *      FF D9, DQT FF DB, SOS FF DA, APP0 FF E0).
  *   The circular approach is required because JPEG frames are variable
  *   size (the snapshot single-buffer trick only works for fixed-size
  *   RGB565 frames where the buffer matches the frame exactly).
  *
  * Verdict printed at the end.
  */

#include "probe_jpeg.h"
#include "bsp_ov5640.h"
#include "bsp_i2c.h"

#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Access to the driver-internal handles/tables (defined in bsp_ov5640.c)
 * needed to reconfigure the DMA stream for circular ring capture.      */
/* ------------------------------------------------------------------ */

extern DMA_HandleTypeDef DMA_Handle_dcmi;   /* defined in bsp_ov5640.c */

/* ------------------------------------------------------------------ */
/* Capture buffer + constants                                          */
/* ------------------------------------------------------------------ */

/* The ring is deliberately modest: snap_buf already occupies 153600 B of
 * the 192 KB SRAM, so this probe uses a 24 KB JPEG ring (=6144 words,
 * still well under the 16-bit NDTR limit). QVGA JPEG frames are typically
 * 8-20 KB at default quality; even a truncated larger frame still shows
 * the leading SOI/DQT/SOS markers. */
#define JPEG_CAP_BUF_SIZE (24u * 1024u)
#define JPEG_CAP_WORDS    (JPEG_CAP_BUF_SIZE / 4u)

__attribute__((aligned(32))) uint8_t jpeg_cap_buf[JPEG_CAP_BUF_SIZE];

/* ------------------------------------------------------------------ */
/* JPEG-marker analysis                                                */
/* ------------------------------------------------------------------ */

typedef struct
{
    uint32_t soi;      /* FF D8 */
    uint32_t eoi;      /* FF D9 */
    uint32_t dqt;      /* FF DB */
    uint32_t sos;      /* FF DA */
    uint32_t app0;     /* FF E0 (JFIF) */
    uint32_t frames;   /* SOI followed by a later EOI */
} cap_analysis_t;

static void analyze_buffer(const uint8_t *buf, uint32_t len, cap_analysis_t *a)
{
    a->soi = a->eoi = a->dqt = a->sos = a->app0 = a->frames = 0;
    if (len < 2) return;

    uint32_t prev_was_ff = 0;
    uint32_t last_soi = 0;

    for (uint32_t i = 0; i < len - 1; i++)
    {
        uint8_t b = buf[i];
        if (prev_was_ff)
        {
            switch (b)
            {
            case 0xD8: a->soi++; last_soi = i; break;
            case 0xD9:
                a->eoi++;
                if (last_soi < i) a->frames++;
                break;
            case 0xDB: a->dqt++; break;
            case 0xDA: a->sos++; break;
            case 0xE0: a->app0++; break;
            case 0xFF: break;                 /* 0xFF 0xFF fill */
            default:   break;
            }
            prev_was_ff = 0;
        }
        else if (b == 0xFF)
        {
            prev_was_ff = 1;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Register helpers                                                    */
/* ------------------------------------------------------------------ */

#define REG_END 0xFFFFu

static void apply_jpeg_regs(const uint16_t (*regs)[2], uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
    {
        if (regs[i][0] == REG_END) break;
        OV5640_WriteReg(regs[i][0], regs[i][1]);
    }
}

static void print_jpeg_ctrl_regs(const char *label)
{
    uint8_t r3002 = OV5640_ReadReg(0x3002);
    uint8_t r3006 = OV5640_ReadReg(0x3006);
    uint8_t r3821 = OV5640_ReadReg(0x3821);
    uint8_t r4713 = OV5640_ReadReg(0x4713);
    uint8_t r4300 = OV5640_ReadReg(0x4300);
    uint8_t r501f = OV5640_ReadReg(0x501f);
    uint8_t r471c = OV5640_ReadReg(0x471c);
    printf("  %s: 0x3002=%02x 0x3006=%02x 0x3821=%02x 0x4713=%02x 0x4300=%02x 0x501f=%02x 0x471c=%02x\r\n",
           label, (unsigned)r3002, (unsigned)r3006, (unsigned)r3821,
           (unsigned)r4713, (unsigned)r4300, (unsigned)r501f, (unsigned)r471c);
}

/* ------------------------------------------------------------------ */
/* Capture: SNAPSHOT (fixed size RGB565 -> snap_buf)                   */
/* ------------------------------------------------------------------ */

static uint32_t capture_snapshot_rgb565(void)
{
    OV5640_FrameState = 0;
    OV5640_DCMI_Resume();
    OV5640_DMA_Config((uint32_t)snap_buf, (uint32_t)(img_width * img_height / 2));

    uint32_t t0 = HAL_GetTick();
    while (!OV5640_FrameState)
    {
        if ((HAL_GetTick() - t0) > 1500u)
        {
            printf("  [snapshot timeout]\r\n");
            return 0;
        }
    }
    return (uint32_t)(img_width * img_height * 2);   /* 153600 B */
}

/* ------------------------------------------------------------------ */
/* Capture: CONTINUOUS ring (variable-size JPEG -> jpeg_cap_buf)       */
/* ------------------------------------------------------------------ */

static void start_jpeg_ring(void)
{
    /* Stop any previous capture/DMA cleanly first. */
    HAL_DCMI_Stop(&DCMI_Handle);

    /* Re-arm DMA2 Stream1 as a CIRCULAR ring into jpeg_cap_buf. */
    __HAL_RCC_DMA2_CLK_ENABLE();
    DMA_Handle_dcmi.Instance = DMA2_Stream1;
    DMA_Handle_dcmi.Init.Channel = DMA_CHANNEL_1;
    DMA_Handle_dcmi.Init.Direction = DMA_PERIPH_TO_MEMORY;
    DMA_Handle_dcmi.Init.PeriphInc = DMA_PINC_DISABLE;
    DMA_Handle_dcmi.Init.MemInc = DMA_MINC_ENABLE;
    DMA_Handle_dcmi.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    DMA_Handle_dcmi.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    DMA_Handle_dcmi.Init.Mode = DMA_CIRCULAR;              /* RING! */
    DMA_Handle_dcmi.Init.Priority = DMA_PRIORITY_HIGH;
    DMA_Handle_dcmi.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
    DMA_Handle_dcmi.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
    DMA_Handle_dcmi.Init.MemBurst = DMA_MBURST_INC4;
    DMA_Handle_dcmi.Init.PeriphBurst = DMA_PBURST_SINGLE;
    __HAL_LINKDMA(&DCMI_Handle, DMA_Handle, DMA_Handle_dcmi);
    HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream1_IRQn);
    HAL_DMA_Init(&DMA_Handle_dcmi);

    /* DCMI CONTINUOUS (not snapshot - frames vary in size). */
    if (DCMI->RISR & DCMI_FLAG_OVRRI) __HAL_DCMI_CLEAR_FLAG(&DCMI_Handle, DCMI_FLAG_OVRRI);
    if (DCMI->RISR & DCMI_FLAG_ERRRI) __HAL_DCMI_CLEAR_FLAG(&DCMI_Handle, DCMI_FLAG_ERRRI);
    HAL_DCMI_Start_DMA(&DCMI_Handle, DCMI_MODE_CONTINUOUS,
                       (uint32_t)jpeg_cap_buf, JPEG_CAP_WORDS);
}

static void run_jpeg_phase(const char *name, const uint16_t (*regs)[2], uint32_t n)
{
    printf("\r\n=== JPEG phase: %s ===\r\n", name);
    apply_jpeg_regs(regs, n);
    print_jpeg_ctrl_regs("ctrl");
    HAL_Delay(250);

    /* Pre-fill the ring with a poison pattern so stale data from a previous
     * phase is unmistakable (0xAA can never be part of a valid JPEG marker
     * preceded by 0xFF, and a stream of 0xAA has no SOI/EOI structure). */
    for (uint32_t i = 0; i < JPEG_CAP_BUF_SIZE; i++)
        jpeg_cap_buf[i] = 0xAA;

    start_jpeg_ring();
    HAL_Delay(1000);                 /* let the ring fill ~1 s */
    HAL_DCMI_Stop(&DCMI_Handle);

    /* How much did the DMA actually write? NDTR counts DOWN from the full
     * transfer length; in circular mode it wraps, so any value != full
     * length proves the stream advanced during this phase. */
    uint16_t ndtr = DMA2_Stream1->NDTR & 0xFFFFu;
    uint32_t written = JPEG_CAP_BUF_SIZE - (uint32_t)(ndtr * 4u);
    printf("  DMA: NDTR=0x%04x (0x%04x minus %u = %u B written from ring start)\r\n",
           (unsigned)ndtr, (unsigned)(JPEG_CAP_WORDS & 0xFFFFu),
           (unsigned)ndtr, (unsigned)written);

    /* A fresh JPEG frame written over the poison shows FF D8 near the start;
     * dump the first SOI's neighborhood to prove it is a real JFIF header. */
    {
        uint32_t soi_idx = 0xFFFFFFFFu;
        for (uint32_t i = 0; i < JPEG_CAP_BUF_SIZE - 1; i++)
        {
            if (jpeg_cap_buf[i] == 0xFF && jpeg_cap_buf[i + 1] == 0xD8)
            {
                soi_idx = i;
                break;
            }
        }
        if (soi_idx != 0xFFFFFFFFu)
        {
            printf("  first SOI @ %u: ", (unsigned)soi_idx);
            for (uint32_t i = 0; i < 24 && (soi_idx + i) < JPEG_CAP_BUF_SIZE; i++)
                printf("%02x ", (unsigned)jpeg_cap_buf[soi_idx + i]);
            printf("\r\n");
            if (soi_idx + 11 < JPEG_CAP_BUF_SIZE &&
                jpeg_cap_buf[soi_idx + 2] == 0xFF &&
                jpeg_cap_buf[soi_idx + 3] == 0xE0 &&
                jpeg_cap_buf[soi_idx + 6] == 'J' &&
                jpeg_cap_buf[soi_idx + 7] == 'F' &&
                jpeg_cap_buf[soi_idx + 8] == 'I' &&
                jpeg_cap_buf[soi_idx + 9] == 'F')
            {
                printf("  => bytes after SOI decode as a JFIF APP0 header\r\n");
            }
            else
            {
                printf("  => SOI present but NOT a JFIF header (unexpected)\r\n");
            }
        }
        else
        {
            printf("  no SOI in the ring this phase\r\n");
        }
    }

    cap_analysis_t a;
    analyze_buffer(jpeg_cap_buf, JPEG_CAP_BUF_SIZE, &a);
    printf("  ring scan:  SOI=%u EOI=%u DQT=%u SOS=%u APP0=%u  complete_frames=%u\r\n",
           (unsigned)a.soi, (unsigned)a.eoi, (unsigned)a.dqt,
           (unsigned)a.sos, (unsigned)a.app0, (unsigned)a.frames);
    if (a.soi == 0)
        printf("  => NO SOI marker - sensor is NOT emitting JPEG frames\r\n");
    else if (a.frames == 0)
        printf("  => SOI found but no complete SOI..EOI - likely truncated/garbage stream\r\n");
    else
        printf("  => %u complete JPEG frame(s) found - sensor CAN emit JPEG\r\n",
               (unsigned)a.frames);
}

/* ------------------------------------------------------------------ */
/* Register lists                                                      */
/* ------------------------------------------------------------------ */

/* JPEG-enable list derived from the OV5640 ref manual (0x3821 bit5 =
 * COMPRESSION ENABLE, §6.1.7) + the proven settings from this repo's
 * eth_http_server ov5640_jpeg_fmt table. */
static const uint16_t jpeg_enable_regs[][2] = {
    {0x3820, 0x40},                /* v-flip */
    {0x3821, 0x20},                /* JPEG/COMPRESSION ENABLE (bit5) */
    {0x4713, 0x02},                /* JPEG mode 2 */
    {0x4300, 0x00},                /* YUV output (JPEG routed via YUV) */
    {0x501f, 0x30},                /* YUYV */
    {0x3002, 0x00},                /* enable JPEG block (was 0x1c) */
    {0x3006, 0xff},                /* enable JPEG clocks (was 0xc3) */
    {0x471c, 0x50},                /* JPEG mode / quant */
    {REG_END, 0x00},
};

/* JPEG mode 3 variant (the vendor RGB565_Init table itself ends with
 * 0x4713=0x03 "JPEG mode 3", so this tests that specific mode). */
static const uint16_t jpeg_mode3_regs[][2] = {
    {0x3821, 0x20},
    {0x4713, 0x03},                /* JPEG mode 3 */
    {0x4300, 0x00},
    {0x501f, 0x30},
    {0x3002, 0x00},
    {0x3006, 0xff},
    {0x471c, 0x50},
    {REG_END, 0x00},
};

/* The esp32-camera driver's sensor_fmt_jpeg list + the compression enable
 * it sets in set_image_options(): */
static const uint16_t esp32_jpeg_regs[][2] = {
    {0x4300, 0x00},                /* FORMAT_CTRL */
    {0x501f, 0x30},                /* FORMAT_CTRL00 */
    {0x3002, 0x00},                /* 0x1c -> 0x00 */
    {0x3006, 0xff},                /* 0xc3 -> 0xff */
    {0x471c, 0x50},                /* 0xd0 -> 0x50 */
    {0x3821, 0x20},                /* compression enable (bit5) */
    {0x4713, 0x02},                /* JPEG mode select 2 */
    {REG_END, 0x00},
};

/* ------------------------------------------------------------------ */
/* Main probe                                                          */
/* ------------------------------------------------------------------ */

void ProbeJpeg_Run(void)
{
    OV5640_IDTypeDef id;

    printf("\r\n=== OV5640 (FD5640) JPEG probe ===\r\n");

    /* 1. HW + SCCB + IDs --------------------------------------------- */
    OV5640_HW_Init();
    I2CMaster_Init();

    OV5640_ReadID(&id);
    uint8_t midh = OV5640_ReadReg(0x300C);
    uint8_t midl = OV5640_ReadReg(0x300D);
    printf("PID = 0x%02x%02x\r\n", (unsigned)id.PIDH, (unsigned)id.PIDL);
    printf("MID = 0x%02x%02x  (genuine OmniVision OV5640 expects 0x7F 0xA2)\r\n",
           (unsigned)midh, (unsigned)midl);
    if (midh == 0x7Fu && midl == 0xA2u)
        printf("  => MID matches genuine OmniVision OV5640\r\n");
    else
        printf("  => MID does NOT match genuine OV5640 - strong clone signal\r\n");

    /* DCMI/DMA init (arms one initial snapshot into snap_buf - harmless). */
    OV5640_Init();

    /* 2. DVP sanity: QVGA RGB565 + color bar, snapshot ------------- */
    printf("\r\n-- DVP sanity: QVGA RGB565 + color bar --\r\n");
    OV5640_RGB565Config();            /* RGB565_Init + RGB565_QVGA table */
    OV5640_WriteReg(0x503D, 0x80);    /* color bar on */
    OV5640_WriteReg(0x4741, 0x00);
    HAL_Delay(200);

    {
        uint32_t got = capture_snapshot_rgb565();
        cap_analysis_t a;
        analyze_buffer(snap_buf, got, &a);
        printf("  snapshot: %u B captured  (SOI=%u EOI=%u DQT=%u SOS=%u)\r\n",
               (unsigned)got, (unsigned)a.soi, (unsigned)a.eoi,
               (unsigned)a.dqt, (unsigned)a.sos);
        if (got < img_width * img_height * 2)
            printf("  WARNING: short capture - is the DVP link delivering data?\r\n");
        else
            printf("  => full QVGA frame arrived - DVP + DCMI + DMA link is LIVE\r\n");
    }

    /* 3. JPEG phases -------------------------------------------------- */
    /* Each phase starts from the same QVGA RGB565 base (vendor tables),
     * then switches the output to JPEG with the listed registers. */
    OV5640_WriteReg(0x503D, 0x00);    /* color bar off */

    run_jpeg_phase("QVGA + JPEG enable (reg list)", jpeg_enable_regs,
                   sizeof(jpeg_enable_regs) / sizeof(jpeg_enable_regs[0]));
    run_jpeg_phase("QVGA + JPEG mode 3 (vendor-style)", jpeg_mode3_regs,
                   sizeof(jpeg_mode3_regs) / sizeof(jpeg_mode3_regs[0]));
    run_jpeg_phase("QVGA + esp32 sensor_fmt_jpeg", esp32_jpeg_regs,
                   sizeof(esp32_jpeg_regs) / sizeof(esp32_jpeg_regs[0]));

    /* 4. Verdict ------------------------------------------------------- */
    printf("\r\n=== JPEG PROBE COMPLETE ===\r\n");
    printf("  Look at the ring-scan SOI counts above.\r\n");
    printf("  - SOI/EOI present  : the sensor CAN emit JPEG (enable it in apps).\r\n");
    printf("  - NO SOI, data ok  : RGB/YUV-only clone without a working JPEG\r\n");
    printf("    encoder (matches esp32-camera issues #307/#368).\r\n");
    printf("  - No data at all   : check PID/MID + the DVP sanity step.\r\n");

    while (1)
    {
    }
}