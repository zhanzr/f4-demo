/**
  * @file    eth_http_server/src/ov5640.c
  * @brief   OV5640 camera driver (see ov5640.h).
  *
  * The OV5640 runs in RGB565 mode at QQVGA (160x120, 38400 bytes/frame) -
  * the vendor-proven stable mode. (The built-in JPEG encoder on this
  * module does not produce valid output through the F4 DCMI: VSYNC fires
  * ~200 Hz with only ~200 bytes/frame captured and no complete SOI/EOI
  * frames - the markers found match random chance.) DCMI runs in normal
  * (non-JPEG) mode with hardware sync; DMA2 Stream1 circularly captures the
  * byte stream into a 115200-byte internal-SRAM ring (exactly 3 frames, so
  * frames never straddle the ring end).
  *
  * Frame extraction is fixed-size: OV5640_GetFrame() hands out complete
  * RGB565 frames from the ring. The HTTP layer converts RGB565 -> RGB888
  * and serves 24-bit BMP (universally supported by browsers).
  *
  * Register tables are from the vendor fire-f429 OV5640 example (RGB565_Init
  * + RGB565_QVGA scaled to QQVGA), which is proven stable on this board.
  * SCCB uses I2C1 (PB6/PB7), same bus as the MPU6050/WM8978 - the driver
  * probes the OV5640 ID (0x56) so the two coexist.
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

/* --- RGB565 QQVGA config -----------------------------------------------------
 * Base = vendor fire-f429 OV5640 init (RGB565_Init, proven stable on this
 * board), output size QQVGA 160x120 via the group-3 mechanism.
 * ------------------------------------------------------------------------ */
static const uint16_t ov5640_base_rgb565[][2] = {
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
    /* image format: RGB565 (vendor) */
    {0x4300, 0x6f},  /* RGB565 */
    {0x501f, 0x01},  /* RGB565 */
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
    /* QQVGA 160x120: vendor RGB565_QVGA timing scaled down, via group-3 */
    {0x3212, 0x03},   /* start group 3 */
    {0x3808, 0x00}, {0x3809, 0xa0},   /* DVPHO = 160 */
    {0x380a, 0x00}, {0x380b, 0x78},   /* DVPVO = 120 */
    {0x3810, 0x00}, {0x3811, 0x10},   /* H offset = 16 */
    {0x3812, 0x00}, {0x3813, 0x04},   /* V offset = 4 */
    {0x3212, 0x13},   /* end group 3 */
    {0x3212, 0xa3},   /* launch group 3 */
    {REG_DLY, 300},   /* let the sensor stream settle */
};

/* RGB565 override table (re-applied by the soft restart): mirrors +
 * format, JPEG blocks off (vendor values). */
static const uint16_t ov5640_rgb565_fmt[][2] = {
    {0x3820, 0x47},   /* vertical flip (vendor RGB565) */
    {0x3821, 0x01},   /* horizontal mirror, JPEG off (bit5=0) */
    {0x4300, 0x6f},   /* RGB565 */
    {0x501f, 0x01},   /* RGB565 */
    {0x3002, 0x1c},   /* JPEG blocks off */
    {0x3006, 0xc3},   /* JPEG clocks off */
};

/* --- Ring buffer + DMA ----------------------------------------------------- */
#define RING_WORDS  (OV5640_FRAME_BUF_SIZE / 4U)

static uint8_t  frame_ring[OV5640_FRAME_BUF_SIZE] __attribute__((section(".sram_dma"), used));
static volatile int camera_ready;

static DCMI_HandleTypeDef hdcmi;
static DMA_HandleTypeDef  hdma_dcmi;

/* --- HAL DCMI callbacks (weak in the HAL, overridden here) ----------------- */

static volatile uint32_t dcmi_frame_evts;
static volatile uint32_t dcmi_vsync_evts;   /* fires EVERY frame */
static volatile uint32_t dcmi_errors;
static volatile uint32_t dcmi_last_err;

void HAL_DCMI_VsyncEventCallback(DCMI_HandleTypeDef *h)
{
    (void)h;
    dcmi_vsync_evts++;
}

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

static uint32_t scan_pos;          /* ring scan position (frame extraction) */
static int dcmi_started;           /* non-zero once DCMI+DMA capture runs */

/* Restart the DCMI+DMA capture (used after a stall/error). Clear the ring
 * so stale frames from before the restart are never mis-scanned as fresh. */
static void ov5640_capture_restart(void)
{
    HAL_DCMI_Stop(&hdcmi);
    HAL_DMA_Abort(&hdma_dcmi);
    memset(frame_ring, 0, sizeof(frame_ring));
    dcmi_dma_init();
    dcmi_init();
    dcmi_started = 1;
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

    /* PWDN PG3 (output, low = power on), RST PG2 (output, 鎸戞垬鑰匜429) */
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
    /* FIFO mode (vendor 45-OV5640 example): without it the DCMI's internal
     * FIFO overruns and 99% of the pixel data is dropped (VSYNC fires at
     * ~200 fps but only ~250 bytes/frame arrive). Burst reads keep up. */
    hdma_dcmi.Init.FIFOMode            = DMA_FIFOMODE_ENABLE;
    hdma_dcmi.Init.FIFOThreshold       = DMA_FIFO_THRESHOLD_FULL;
    hdma_dcmi.Init.MemBurst            = DMA_MBURST_INC4;
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
    hdcmi.Init.JPEGMode         = DCMI_JPEG_DISABLE;   /* RGB565 normal mode */
    HAL_DCMI_Init(&hdcmi);

    /* Start continuous capture into the ring (single circular DMA). */
    HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_CONTINUOUS,
                       (uint32_t)frame_ring, RING_WORDS);
}

/* --- Frame extraction (fixed-size, ring = 3 frame slots) ------------------ */

static volatile uint32_t g_last_frame_tick;   /* HAL_GetTick() of last frame */
static volatile uint32_t g_frame_count;       /* frames found since boot */

/* The ring holds exactly 3 frame slots (OV5640_FRAME_BUF_SIZE is 3x the
 * frame size), so a frame never straddles the ring end. scan_pos is the
 * ring offset of the next unread frame (always a multiple of
 * OV5640_FRAME_BYTES). A frame is complete when the DMA has written
 * past scan_pos + FRAME_BYTES. */
int OV5640_GetFrame(const uint8_t **frame)
{
    uint32_t size  = OV5640_FRAME_BUF_SIZE;
    uint32_t ndtr  = hdma_dcmi.Instance->NDTR;
    uint32_t write = (RING_WORDS - ndtr) * 4U;

    if (write < scan_pos)
    {
        /* The DMA wrapped past our read position: frames were missed.
         * Resync to the start of the newest (possibly partial) frame so
         * the reader never deadlocks while the DMA keeps cycling. */
        scan_pos = write - (write % OV5640_FRAME_BYTES);
    }

    if ((write - scan_pos) < OV5640_FRAME_BYTES)
    {
        return 0;   /* frame not complete yet */
    }

    *frame = &frame_ring[scan_pos];
    scan_pos += OV5640_FRAME_BYTES;
    if (scan_pos >= size) scan_pos = 0;

    g_last_frame_tick = HAL_GetTick();
    g_frame_count++;
    return 1;
}

uint32_t OV5640_FrameCount(void)
{
    return g_frame_count;
}

/* Soft restart (no power-cycle): re-apply the RGB565 format table and
 * re-launch the timing group, then clear the ring. RGB565 is the stable
 * mode, so this is just a safety net if the sensor ever stalls. */
static int ov5640_soft_restart(void)
{
    ov5640_write_table(ov5640_rgb565_fmt,
                       sizeof(ov5640_rgb565_fmt) / sizeof(ov5640_rgb565_fmt[0]));
    if (ov5640_write_reg(0x3212, 0x03) != 0) return -1;   /* group 3 start */
    if (ov5640_write_reg(0x3814, 0x31) != 0) return -1;
    if (ov5640_write_reg(0x3815, 0x31) != 0) return -1;
    if (ov5640_write_reg(0x3212, 0x13) != 0) return -1;   /* end group 3   */
    if (ov5640_write_reg(0x3212, 0xa3) != 0) return -1;   /* launch        */
    memset(frame_ring, 0, sizeof(frame_ring));
    scan_pos = 0;
    return 0;
}

/* Health check: RGB565 is the vendor-proven stable mode, so this is just
 * a safety net. The stall is detected from the DMA write position (NDTR),
 * NOT from consumed frames - frames are only consumed while a browser is
 * streaming, so frame-based detection would falsely restart a healthy
 * sensor when nobody is connected. If the DMA stops moving for 2 s, do a
 * soft restart (re-apply format + re-launch timing), then a full re-init.
 * Call periodically from the main loop. */
void OV5640_HealthCheck(void)
{
    uint32_t now;
    uint32_t ndtr = hdma_dcmi.Instance->NDTR;
    uint32_t write = (RING_WORDS - ndtr) * 4U;
    static uint32_t last_w;
    static uint32_t last_mv;      /* tick of last DMA movement */
    static uint32_t last_msg;

    if (!camera_ready) return;

    now = HAL_GetTick();

    if (last_mv == 0U)
    {
        last_mv = now;
        last_w  = write;
        return;
    }

    /* DMA still moving? */
    if (write != last_w)
    {
        last_w  = write;
        last_mv = now;
        return;
    }

    /* DMA frozen for 2 s: sensor stalled. */
    if ((now - last_mv) < 2000U) return;

    if (now - last_msg >= 10000U)
    {
        printf("OV5640: DMA frozen (ndtr=%lu) - soft restarting\r\n",
               (unsigned long)ndtr);
        last_msg = now;
    }

    if (ov5640_soft_restart() == 0)
    {
        last_mv = HAL_GetTick();
        last_w  = (RING_WORDS - hdma_dcmi.Instance->NDTR) * 4U;
        return;
    }

    /* Soft restart failed: full re-init (also rate-limited). */
    if (now - last_msg >= 10000U)
    {
        printf("OV5640: soft restart failed, full re-init\r\n");
        last_msg = now;
    }
    if (OV5640_Init() != 0)
    {
        camera_ready = 0;
        printf("OV5640: re-init failed, camera disabled\r\n");
    }
}

int OV5640_Ready(void)
{
    return camera_ready;
}

/* --- Boot diagnostics ------------------------------------------------------- */
void OV5640_Selftest(void)
{
    const uint8_t *frame;
    uint32_t frames = 0;
    uint32_t t0 = HAL_GetTick();

    /* Count RGB565 frames for 1.5 s. */
    while ((HAL_GetTick() - t0) < 1500U)
    {
        if (OV5640_GetFrame(&frame)) frames++;
    }

    uint32_t dt = HAL_GetTick() - t0;
    if (frames)
    {
        printf("OV5640: selftest OK - %lu RGB565 frames (%ux%u), %lu fps\r\n",
               (unsigned long)frames, (unsigned)OV5640_FRAME_W,
               (unsigned)OV5640_FRAME_H,
               (unsigned long)(frames * 1000U / (dt ? dt : 1U)));
    }
    else
    {
        printf("OV5640: selftest FAILED - no RGB565 frames\r\n");
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

    /* Re-init (health watchdog): stop the old capture before reconfiguring,
     * otherwise HAL_DMA_Init fails on the busy stream. */
    if (dcmi_started)
    {
        HAL_DCMI_Stop(&hdcmi);
        HAL_DMA_Abort(&hdma_dcmi);
        dcmi_started = 0;
    }

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
        ov5640_write_table(ov5640_base_rgb565,
                           sizeof(ov5640_base_rgb565) / sizeof(ov5640_base_rgb565[0]));
        ov5640_write_table(ov5640_rgb565_fmt,
                           sizeof(ov5640_rgb565_fmt) / sizeof(ov5640_rgb565_fmt[0]));

        /* Bring up DCMI + DMA capture. */
        dcmi_dma_init();
        /* Clear the ring BEFORE capture starts so the frame-verify below
         * only counts fresh sensor data. */
        memset(frame_ring, 0, sizeof(frame_ring));
        dcmi_init();
        dcmi_started = 1;

        /* Wait ~1.5 s for the first complete RGB565 frame. */
        scan_pos = 0;
        dcmi_frame_evts = 0;
        dcmi_vsync_evts = 0;
        g_frame_count = 0;
        g_last_frame_tick = 0;
        {
            const uint8_t *f;
            int got = 0;
            uint32_t t0 = HAL_GetTick();
            while ((HAL_GetTick() - t0) < 1500U)
            {
                if (OV5640_GetFrame(&f)) { got = 1; break; }
            }
            if (got)
            {
                printf("OV5640: init ok (attempt %d, RGB565 %ux%u)\r\n",
                       attempt, (unsigned)OV5640_FRAME_W,
                       (unsigned)OV5640_FRAME_H);
                camera_ready = 1;
                return 0;
            }
        }

        printf("OV5640: init attempt %d: no frames (vsync=%lu ndtr=%lu "
               "ndtr0=%lu), power-cycling...\r\n",
               attempt, (unsigned long)dcmi_vsync_evts,
               (unsigned long)hdma_dcmi.Instance->NDTR,
               (unsigned long)RING_WORDS);
        HAL_DCMI_Stop(&hdcmi);
        HAL_DMA_Abort(&hdma_dcmi);
        ov5640_power_on();
    }

    printf("OV5640: init FAILED after 4 attempts\r\n");
    return -1;
}
