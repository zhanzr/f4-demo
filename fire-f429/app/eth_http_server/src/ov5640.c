/**
  * @file    eth_http_server/src/ov5640.c
  * @brief   OV5640 camera driver (see ov5640.h).
  *
  * The OV5640 is configured for QVGA (320x240) direct JPEG output (its
  * built-in JPEG encoder). The DCMI peripheral runs in continuous JPEG mode
  * with hardware sync, and DMA2 Stream1 circularly captures the byte stream
  * into a 128 KB internal-SRAM ring.
  *
  * Zero-copy frame extraction: the HTTP MJPEG stream handler calls
  * OV5640_GetJpegFrame(), which reads the DMA write position (from NDTR),
  * scans the newly-written region for a JPEG SOI (FF D8) then EOI (FF D9),
  * and returns a pointer into the ring. Because QVGA JPEG frames (typically
  * 20-60 KB) are much smaller than the 128 KB ring, the DMA is always
  * several frames behind the read pointer, so complete frames are found
  * without copying and without wrap-around issues in practice.
  *
  * Register tables are merged from the esp32-camera OV5640 driver (JPEG
  * mode, QVGA 320x240 timing) and the vendor fire-f429 OV5640 example (pin
  * mapping). SCCB uses I2C1 (PB6/PB7), same bus as the MPU6050/WM8978 - the
  * driver probes the OV5640 ID (0x56) so the two coexist.
  */

#include "ov5640.h"
#include <stdio.h>
#include <string.h>

/* --- Pins (vendor 45-OV5640 example) ------------------------------------- */
#define DCMI_VSYNC_PORT   GPIOI
#define DCMI_VSYNC_PIN    GPIO_PIN_5
#define DCMI_HSYNC_PORT   GPIOA
#define DCMI_HSYNC_PIN    GPIO_PIN_4
#define DCMI_PIXCLK_PORT  GPIOA
#define DCMI_PIXCLK_PIN   GPIO_PIN_6
#define DCMI_D0_PORT      GPIOH
#define DCMI_D0_PIN       GPIO_PIN_9
#define DCMI_D1_PORT      GPIOH
#define DCMI_D1_PIN       GPIO_PIN_10
#define DCMI_D2_PORT      GPIOH
#define DCMI_D2_PIN       GPIO_PIN_11
#define DCMI_D3_PORT      GPIOH
#define DCMI_D3_PIN       GPIO_PIN_12
#define DCMI_D4_PORT      GPIOH
#define DCMI_D4_PIN       GPIO_PIN_14
#define DCMI_D5_PORT      GPIOD
#define DCMI_D5_PIN       GPIO_PIN_3
#define DCMI_D6_PORT      GPIOI
#define DCMI_D6_PIN       GPIO_PIN_6
#define DCMI_D7_PORT      GPIOI
#define DCMI_D7_PIN       GPIO_PIN_7
#define DCMI_PWDN_PORT    GPIOG
#define DCMI_PWDN_PIN     GPIO_PIN_3
#define DCMI_RST_PORT     GPIOG
#define DCMI_RST_PIN      GPIO_PIN_2

#define OV5640_AF_DCMI    GPIO_AF13_DCMI

/* --- SCCB over I2C1 ------------------------------------------------------- */
static I2C_HandleTypeDef cam_i2c;
static volatile uint32_t ov5640_i2c_errors;

static void ov5640_i2c_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    gpio.Pin       = GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode      = GPIO_MODE_AF_OD;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &gpio);

    cam_i2c.Instance             = I2C1;
    cam_i2c.Init.ClockSpeed      = 400000U;
    cam_i2c.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    cam_i2c.Init.OwnAddress1     = 0U;
    cam_i2c.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    cam_i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    cam_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    cam_i2c.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&cam_i2c);
}

static int ov5640_write_reg(uint16_t reg, uint8_t val)
{
    uint8_t buf[3];
    buf[0] = (uint8_t)(reg >> 8);
    buf[1] = (uint8_t)(reg & 0xFF);
    buf[2] = val;
    if (HAL_I2C_Master_Transmit(&cam_i2c, OV5640_SCCB_ADDR, buf, 3U, 100U)
        == HAL_OK)
    {
        return 0;
    }
    ov5640_i2c_errors++;
    return -1;
}

static int ov5640_read_reg(uint16_t reg, uint8_t *val)
{
    uint8_t addr[2];
    addr[0] = (uint8_t)(reg >> 8);
    addr[1] = (uint8_t)(reg & 0xFF);
    if (HAL_I2C_Master_Transmit(&cam_i2c, OV5640_SCCB_ADDR, addr, 2U, 100U) != HAL_OK)
    {
        return -1;
    }
    if (HAL_I2C_Master_Receive(&cam_i2c, OV5640_SCCB_ADDR | 0x01U, val, 1U, 100U)
        == HAL_OK)
    {
        return 0;
    }
    ov5640_i2c_errors++;
    return -1;
}

#define REG_DLY 0xFFFFU

static void ov5640_write_table(const uint16_t tab[][2], uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
    {
        if (tab[i][0] == REG_DLY)
        {
            HAL_Delay(tab[i][1]);
        }
        else
        {
            (void)ov5640_write_reg(tab[i][0], (uint8_t)tab[i][1]);
        }
    }
}

/* --- JPEG config ------------------------------------------------------------
 * Base = vendor fire-f429 OV5640 init (RGB565_Init, proven on this board),
 * with JPEG format applied afterwards. QVGA 320x240 via the register
 * group-3 mechanism (vendor RGB565_QVGA).
 * ------------------------------------------------------------------------ */
static const uint16_t ov5640_base_jpeg[][2] = {
    {0x3103, 0x11},  /* system clock from pad */
    {0x3008, 0x82},  /* software reset */
    {REG_DLY, 10},
    {0x3008, 0x42},  /* power down */
    {0x3103, 0x03},  /* system clock from PLL */
    {0x3017, 0xff},  /* FREX, Vsync, HREF, PCLK, D[9:6] output enable */
    {0x3018, 0xff},  /* D[5:0], GPIO[1:0] output enable */
    {0x3034, 0x1a},  /* MIPI 10-bit */
    {0x3037, 0x13},  /* PLL root divider, pre-divider */
    {0x3108, 0x01},  /* PCLK root divider */
    {0x4713, 0x02},  /* jpg mode select */
    {0x3630, 0x36}, {0x3631, 0x0e}, {0x3632, 0xe2}, {0x3633, 0x12},
    {0x3621, 0xe0}, {0x3704, 0xa0}, {0x3703, 0x5a}, {0x3715, 0x78},
    {0x3717, 0x01}, {0x370b, 0x60}, {0x3705, 0x1a}, {0x3905, 0x02},
    {0x3906, 0x10}, {0x3901, 0x0a}, {0x3731, 0x12}, {0x3600, 0x08},
    {0x3601, 0x33}, {0x302d, 0x60}, {0x3620, 0x52}, {0x371b, 0x20},
    {0x471c, 0x50}, {0x3a13, 0x43}, {0x3a18, 0x00}, {0x3a19, 0xf8},
    {0x3635, 0x13}, {0x3636, 0x03}, {0x3634, 0x40}, {0x3622, 0x01},
    /* 50/60Hz detection */
    {0x3c01, 0x34}, {0x3c04, 0x28}, {0x3c05, 0x98}, {0x3c06, 0x00},
    {0x3c07, 0x08}, {0x3c08, 0x00}, {0x3c09, 0x1c}, {0x3c0a, 0x9c},
    {0x3c0b, 0x40},
    /* timing */
    {0x3810, 0x00}, {0x3811, 0x10}, {0x3812, 0x00}, {0x3708, 0x64},
    {0x4001, 0x02}, {0x4005, 0x1a}, {0x3000, 0x00}, {0x3004, 0xff},
    {0x3002, 0x1c}, {0x3006, 0xc3},   /* system clocks (esp32 base) */
    {0x300e, 0x58}, {0x302e, 0x00},
    /* image format: JPEG */
    {0x4300, 0x00},  /* YUV422 */
    {0x501f, 0x30},  /* YUYV */
    {0x440e, 0x00},
    {0x5000, 0xa7},  /* Lenc on, raw gamma on, BPC on, WPC on, CIP on */
    /* AEC target */
    {0x3a0f, 0x30}, {0x3a10, 0x28}, {0x3a1b, 0x30}, {0x3a1e, 0x26},
    {0x3a11, 0x60}, {0x3a1f, 0x14},
    /* AWB */
    {0x5180, 0xff}, {0x5181, 0xf2}, {0x5182, 0x00}, {0x5183, 0x14},
    {0x5184, 0x25}, {0x5185, 0x24}, {0x5186, 0x09}, {0x5187, 0x09},
    {0x5188, 0x09}, {0x5189, 0x75}, {0x518a, 0x54}, {0x518b, 0xe0},
    {0x518c, 0xb2}, {0x518d, 0x42}, {0x518e, 0x3d}, {0x518f, 0x56},
    {0x5190, 0x46}, {0x5191, 0xf8}, {0x5192, 0x04}, {0x5193, 0x70},
    {0x5194, 0xf0}, {0x5195, 0xf0}, {0x5196, 0x03}, {0x5197, 0x01},
    {0x5198, 0x04}, {0x5199, 0x12}, {0x519a, 0x04}, {0x519b, 0x00},
    {0x519c, 0x06}, {0x519d, 0x82}, {0x519e, 0x38},
    /* gamma */
    {0x5480, 0x01}, {0x5481, 0x08}, {0x5482, 0x14}, {0x5483, 0x28},
    {0x5484, 0x51}, {0x5485, 0x65}, {0x5486, 0x71}, {0x5487, 0x7d},
    {0x5488, 0x87}, {0x5489, 0x91}, {0x548a, 0x9a}, {0x548b, 0xaa},
    {0x548c, 0xb8}, {0x548d, 0xcd}, {0x548e, 0xdd}, {0x548f, 0xea},
    {0x5490, 0x1d},
    /* color matrix */
    {0x5381, 0x1e}, {0x5382, 0x5b}, {0x5383, 0x08}, {0x5384, 0x0a},
    {0x5385, 0x7e}, {0x5386, 0x88}, {0x5387, 0x7c}, {0x5388, 0x6c},
    {0x5389, 0x10}, {0x538a, 0x01}, {0x538b, 0x98},
    /* UV adjust */
    {0x5580, 0x06}, {0x5583, 0x40}, {0x5584, 0x10}, {0x5589, 0x10},
    {0x558a, 0x00}, {0x558b, 0xf8}, {0x501d, 0x40},
    /* CIP */
    {0x5300, 0x08}, {0x5301, 0x30}, {0x5302, 0x10}, {0x5303, 0x00},
    {0x5304, 0x08}, {0x5305, 0x30}, {0x5306, 0x08}, {0x5307, 0x16},
    {0x5309, 0x08}, {0x530a, 0x30}, {0x530b, 0x04}, {0x530c, 0x06},
    {0x5025, 0x00}, {0x3008, 0x02},  /* wake up from standby */
    /* QVGA 320x240: vendor RGB565_QVGA via group-3 mechanism */
    {0x3212, 0x03},   /* start group 3 */
    {0x3808, 0x01}, {0x3809, 0x40},   /* DVPHO = 320 */
    {0x380a, 0x00}, {0x380b, 0xf0},   /* DVPVO = 240 */
    {0x3810, 0x00}, {0x3811, 0x10},   /* H offset = 16 */
    {0x3812, 0x00}, {0x3813, 0x04},   /* V offset = 4 */
    {0x3212, 0x13},   /* end group 3 */
    {0x3212, 0xa3},   /* launch group 3 */
    {REG_DLY, 300},   /* let the sensor stream settle before fmt change */
};


/* JPEG format enable: switch the sensor output to the JPEG encoder.
 * Key registers (OV5640 reference manual 6.1.7):
 *   0x3821 bit5  - JPEG enable (COMPRESSION ENABLE)
 *   0x4713       - JPEG mode select (default 0x02 = JPEG mode 2)
 *   0x4300/0x501f- YUV422 input to the encoder
 *   0x3002/0x3006/0x471c - clock enables / dividers (esp32 fmt_jpeg) */
static const uint16_t ov5640_jpeg_fmt[][2] = {
    {0x3820, 0x40},   /* no binning */
    {0x3821, 0x20},   /* JPEG enable (bit5) */
    {0x4713, 0x02},   /* jpg mode select (mode 2) */
    {0x4300, 0x00},   /* YUV422 */
    {0x501f, 0x30},   /* YUYV */
    {0x3002, 0x00},   /* 0x1c -> 0x00 */
    {0x3006, 0xff},   /* 0xc3 -> 0xff */
    {0x471c, 0x50},   /* 0xd0 -> 0x50 */
};

/* --- Ring buffer + DMA ----------------------------------------------------- */
#define RING_WORDS  (OV5640_FRAME_BUF_SIZE / 4U)

static uint8_t  jpeg_ring[OV5640_FRAME_BUF_SIZE] __attribute__((section(".sram_dma"), used));
static volatile int camera_ready;

static DCMI_HandleTypeDef hdcmi;
static DMA_HandleTypeDef  hdma_dcmi;

/* --- HAL DCMI callbacks (weak in the HAL, overridden here) ----------------- */

void HAL_DCMI_VsyncEventCallback(DCMI_HandleTypeDef *h)
{
    (void)h;
}

static volatile uint32_t dcmi_frame_evts;
static volatile uint32_t dcmi_errors;
static volatile uint32_t dcmi_last_err;

void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *h)
{
    (void)h;
    dcmi_frame_evts++;
}

/* The HAL aborts the DMA on a DCMI sync error or FIFO overflow - capture
 * would silently stop. Record it; OV5640_GetJpegFrame() restarts capture
 * when it detects the stall (NDTR frozen). */
void HAL_DCMI_ErrorCallback(DCMI_HandleTypeDef *h)
{
    dcmi_last_err = h->ErrorCode;
    dcmi_errors++;
}

/* --- IRQ handlers ----------------------------------------------------------- */
void DCMI_IRQHandler(void)
{
    /* A transient sync error should not abort the JPEG capture: clear the
     * error flags before the HAL sees them (the HAL aborts the DMA on any
     * DCMI error). */
    if (DCMI->RISR & DCMI_FLAG_ERRRI)
    {
        __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_ERRRI);
        dcmi_errors++;
    }
    if (DCMI->RISR & DCMI_FLAG_OVRRI)
    {
        __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_OVRRI);
        dcmi_errors++;
    }
    HAL_DCMI_IRQHandler(&hdcmi);
}

void DMA2_Stream1_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_dcmi);
}

static void dcmi_dma_init(void);
static void dcmi_init(void);

static uint32_t scan_pos;    /* ring scan position (frame extraction) */

/* Restart the DCMI+DMA capture (used after a stall/error). */
static void ov5640_capture_restart(void)
{
    HAL_DCMI_Stop(&hdcmi);
    HAL_DMA_Abort(&hdma_dcmi);
    dcmi_dma_init();
    dcmi_init();
    scan_pos = 0;
}

/* Detect a stalled capture (DCMI stopped feeding the DMA) and restart it.
 * Call before scanning for frames. */
static void ov5640_capture_check(void)
{
    static uint32_t last_ndtr = 0xFFFFFFFFU;
    static uint32_t last_tick = 0;
    uint32_t ndtr = hdma_dcmi.Instance->NDTR;
    uint32_t now  = HAL_GetTick();

    if (last_ndtr == 0xFFFFFFFFU)
    {
        last_ndtr = ndtr;
        last_tick = now;
        return;
    }

    if (ndtr != last_ndtr)
    {
        last_ndtr = ndtr;
        last_tick = now;
        return;
    }

    /* NDTR unchanged; restart only if it stayed frozen for 2 s (the JPEG
     * encoder outputs in bursts, so short idle gaps are normal). */
    if ((now - last_tick) < 2000U) return;

    printf("OV5640: stall (ndtr=%lu err=%lu), restarting capture\r\n",
           (unsigned long)ndtr, (unsigned long)dcmi_errors);
    dcmi_errors = 0;
    last_ndtr   = 0xFFFFFFFFU;
    ov5640_capture_restart();
}

/* --- GPIO / DCMI / DMA init ------------------------------------------------ */

static void ov5640_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();

    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = OV5640_AF_DCMI;

    /* HSYNC PA4, PIXCLK PA6 */
    gpio.Pin = DCMI_HSYNC_PIN | DCMI_PIXCLK_PIN;
    HAL_GPIO_Init(GPIOA, &gpio);
    /* VSYNC PI5 */
    gpio.Pin = DCMI_VSYNC_PIN;
    HAL_GPIO_Init(DCMI_VSYNC_PORT, &gpio);
    /* D0..D3 PH9..12, D4 PH14 */
    gpio.Pin = DCMI_D0_PIN | DCMI_D1_PIN | DCMI_D2_PIN | DCMI_D3_PIN | DCMI_D4_PIN;
    HAL_GPIO_Init(GPIOH, &gpio);
    /* D5 PD3 */
    gpio.Pin = DCMI_D5_PIN;
    HAL_GPIO_Init(DCMI_D5_PORT, &gpio);
    /* D6 PI6, D7 PI7 */
    gpio.Pin = DCMI_D6_PIN | DCMI_D7_PIN;
    HAL_GPIO_Init(GPIOI, &gpio);

    /* PWDN PG3 (output, low = power on), RST PG2 (output, 挑战者F429) */
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin   = DCMI_PWDN_PIN;
    HAL_GPIO_Init(DCMI_PWDN_PORT, &gpio);
    gpio.Pin   = DCMI_RST_PIN;
    HAL_GPIO_Init(DCMI_RST_PORT, &gpio);
}

static void ov5640_power_on(void)
{
    HAL_GPIO_WritePin(DCMI_RST_PORT, DCMI_RST_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(DCMI_PWDN_PORT, DCMI_PWDN_PIN, GPIO_PIN_SET);   /* power down */
    HAL_Delay(20);
    HAL_GPIO_WritePin(DCMI_PWDN_PORT, DCMI_PWDN_PIN, GPIO_PIN_RESET); /* power on  */
    HAL_Delay(50);
    HAL_GPIO_WritePin(DCMI_RST_PORT, DCMI_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(100);
}

void HAL_DCMI_MspInit(DCMI_HandleTypeDef *h)
{
    (void)h;
    __HAL_RCC_DCMI_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
}

static void dcmi_dma_init(void)
{
    __HAL_RCC_DMA2_CLK_ENABLE();

    hdma_dcmi.Instance                 = DMA2_Stream1;
    hdma_dcmi.Init.Channel             = DMA_CHANNEL_1;
    hdma_dcmi.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_dcmi.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_dcmi.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_dcmi.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_dcmi.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    hdma_dcmi.Init.Mode                = DMA_CIRCULAR;
    hdma_dcmi.Init.Priority            = DMA_PRIORITY_HIGH;
    hdma_dcmi.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    hdma_dcmi.Init.FIFOThreshold       = DMA_FIFO_THRESHOLD_FULL;
    hdma_dcmi.Init.MemBurst            = DMA_MBURST_SINGLE;
    hdma_dcmi.Init.PeriphBurst         = DMA_PBURST_SINGLE;
    HAL_DMA_Init(&hdma_dcmi);

    __HAL_LINKDMA(&hdcmi, DMA_Handle, hdma_dcmi);

    HAL_NVIC_SetPriority(DCMI_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DCMI_IRQn);
    HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream1_IRQn);
}

static void dcmi_init(void)
{
    __HAL_RCC_DCMI_CLK_ENABLE();

    hdcmi.Instance              = DCMI;
    hdcmi.Init.SynchroMode      = DCMI_SYNCHRO_HARDWARE;
    hdcmi.Init.PCKPolarity      = DCMI_PCKPOLARITY_RISING;
    hdcmi.Init.VSPolarity       = DCMI_VSPOLARITY_HIGH;
    hdcmi.Init.HSPolarity       = DCMI_HSPOLARITY_LOW;
    hdcmi.Init.CaptureRate      = DCMI_CR_ALL_FRAME;
    hdcmi.Init.ExtendedDataMode = DCMI_EXTEND_DATA_8B;
    hdcmi.Init.JPEGMode         = DCMI_JPEG_ENABLE;   /* variable-length JPEG */
    HAL_DCMI_Init(&hdcmi);

    /* Start continuous capture into the ring (single circular DMA). */
    HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_CONTINUOUS,
                       (uint32_t)jpeg_ring, RING_WORDS);
}

/* --- Frame extraction (zero-copy scan of the ring) ------------------------- */

uint32_t OV5640_GetJpegFrame(const uint8_t **frame)
{
    /* If a DCMI error aborted the capture, restart it before scanning. */
    ov5640_capture_check();

    /* The ring is small (128 KB) and the DMA wraps continuously, so scan
     * the whole ring for complete JPEG frames (FF D8 FF ... FF D9) and
     * return the LAST one found. A ~128 KB scan is ~0.5 ms at 180 MHz. */
    uint32_t size  = RING_WORDS * 4U;
    uint32_t best_len = 0;
    const uint8_t *best = NULL;
    uint32_t i = 0;

    while (i + 2 < size)
    {
        if (jpeg_ring[i] == 0xFF && jpeg_ring[i + 1] == 0xD8 &&
            jpeg_ring[i + 2] == 0xFF)
        {
            uint32_t j = i + 2;
            while (j + 1 < size)
            {
                if (jpeg_ring[j] == 0xFF && jpeg_ring[j + 1] == 0xD9)
                {
                    uint32_t flen = (j + 2) - i;
                    if (flen >= OV5640_MIN_JPEG_LEN)
                    {
                        best     = &jpeg_ring[i];
                        best_len = flen;
                    }
                    i = j + 2;       /* continue after this span */
                    break;           /* inner loop */
                }
                j++;
            }
            if (j + 1 >= size)
            {
                /* SOI without a complete frame (partial write at ring
                 * tail): skip past it byte-wise and keep scanning. */
                i++;
            }
        }
        else
        {
            i++;
        }
    }

    if (best)
    {
        *frame = best;
        return best_len;
    }
    return 0;
}

int OV5640_Ready(void)
{
    return camera_ready;
}

/* --- Boot diagnostics ------------------------------------------------------- */
void OV5640_Selftest(void)
{
    const uint8_t *frame;
    uint32_t len, frames = 0, last = 0;
    uint32_t t0 = HAL_GetTick();

    /* Count JPEG frames for 1.5 s. */
    while ((HAL_GetTick() - t0) < 1500U)
    {
        len = OV5640_GetJpegFrame(&frame);
        if (len)
        {
            frames++;
            last = len;
        }
    }

    uint32_t dt = HAL_GetTick() - t0;
    if (frames)
    {
        printf("OV5640: selftest OK - %lu JPEG frames, last %luB, %lu fps\r\n",
               (unsigned long)frames, (unsigned long)last,
               (unsigned long)(frames * 1000U / (dt ? dt : 1U)));
    }
    else
    {
        printf("OV5640: selftest FAILED - no JPEG frames\r\n");
    }
}

/* --- I2C bus diagnostics ---------------------------------------------------- */
static void ov5640_i2c_scan(void)
{
    int first = 1;

    printf("OV5640: I2C1 scan (8-bit addrs):");
    for (uint16_t a = 0x08; a <= 0xF8; a += 2)
    {
        if (HAL_I2C_IsDeviceReady(&cam_i2c, a, 2, 10U) == HAL_OK)
        {
            printf(" %02x", (unsigned)a);
            first = 0;
        }
    }
    if (first)
    {
        printf(" (none)");
    }
    printf("\r\n");
}

/* --- Init ------------------------------------------------------------------- */
int OV5640_Init(void)
{
    uint8_t idh = 0, idl = 0;

    ov5640_gpio_init();
    ov5640_power_on();
    ov5640_i2c_init();

    /* Probe the sensor ID (0x300A/0x300B -> 0x56 0x40). */
    if (ov5640_read_reg(0x300A, &idh) != 0 || ov5640_read_reg(0x300B, &idl) != 0)
    {
        ov5640_i2c_scan();
        printf("OV5640: SCCB read failed (idh=%02x idl=%02x)\r\n",
               (unsigned)idh, (unsigned)idl);
        return -1;   /* SCCB not responding */
    }
    if (idh != 0x56)
    {
        ov5640_i2c_scan();
        printf("OV5640: wrong ID (idh=%02x idl=%02x)\r\n",
               (unsigned)idh, (unsigned)idl);
        return -1;   /* not an OV5640 */
    }

    /* Configure + verify. The module's 24 MHz crystal start-up is
     * occasionally slow, so retry the whole config (with a power-cycle)
     * until JPEG frames actually flow. */
    for (int attempt = 1; attempt <= 4; attempt++)
    {
        /* Configure JPEG QVGA output (vendor base + JPEG fmt). */
        ov5640_write_table(ov5640_base_jpeg,
                           sizeof(ov5640_base_jpeg) / sizeof(ov5640_base_jpeg[0]));
        ov5640_write_table(ov5640_jpeg_fmt,
                           sizeof(ov5640_jpeg_fmt) / sizeof(ov5640_jpeg_fmt[0]));

        /* Bring up DCMI + DMA capture. */
        dcmi_dma_init();
        dcmi_init();

        /* Wait ~1.2 s for a complete JPEG frame. */
        scan_pos = 0;
        dcmi_frame_evts = 0;
        {
            const uint8_t *f;
            uint32_t flen = 0;
            uint32_t t0 = HAL_GetTick();
            while ((HAL_GetTick() - t0) < 1200U)
            {
                flen = OV5640_GetJpegFrame(&f);
                if (flen) break;
            }
            if (flen)
            {
                printf("OV5640: init ok (attempt %d, %luB frame)\r\n",
                       attempt, (unsigned long)flen);
                camera_ready = 1;
                return 0;
            }
        }

        printf("OV5640: init attempt %d: no frames, power-cycling...\r\n",
               attempt);
        HAL_DCMI_Stop(&hdcmi);
        HAL_DMA_Abort(&hdma_dcmi);
        ov5640_power_on();
    }

    printf("OV5640: init FAILED after 4 attempts\r\n");
    return -1;
}
