/**
  * @file    eth_http_server/src/ov5640.c
  * @brief   OV5640 camera driver (see ov5640.h).
  *
  * The OV5640 runs in JPEG mode at QVGA (320x240). The sensor's built-in
  * JPEG encoder is enabled (0x3821 bit5 COMPRESSION ENABLE); this was
  * hardware-verified by app/jpeg_test on the "FD5640" module of this board
  * (valid JFIF: FF D8 FF E0 'JFIF' ... FF DB ... FF DA ... FF D9).
  *
  * Capture = DCMI CONTINUOUS + DMA2 CIRCULAR ring into internal SRAM. JPEG
  * frames are VARIABLE-SIZE, so the old fixed-size SNAPSHOT path (which only
  * works for RGB565 where the buffer == frame) is replaced by a ring: the
  * DMA keeps writing into a 32 KB circular buffer; OV5640_GetFrame() scans
  * for a complete SOI..EOI pair and hands out the frame. This is the
  * app/jpeg_test-proven method.
  *
  * The HTTP layer serves the raw JPEG bytes as image/jpeg - no RGB565 ->
  * BMP conversion, ~10x less bandwidth than the previous 230 KB BMP frames.
  *
  * Register tables are from the vendor fire-f429 OV5640 example (RGB565_Init
  * + RGB565_QVGA geometry) with the output format switched to JPEG.
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

/* --- RGB565 QVGA config -----------------------------------------------------
 * Base = vendor fire-f429 OV5640 init (RGB565_Init, proven stable on this
 * board), output size QVGA 320x240 via the vendor RGB565_QVGA timing.
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
    /* QVGA 320x240 (vendor RGB565_QVGA timing group 3). */
    {0x3212, 0x03},   /* start group 3 */
    {0x3808, 0x01}, {0x3809, 0x40},   /* DVPHO = 320 */
    {0x380a, 0x00}, {0x380b, 0xf0},   /* DVPVO = 240 */
    {0x3810, 0x00}, {0x3811, 0x10},   /* H offset = 16 */
    {0x3812, 0x00}, {0x3813, 0x04},   /* V offset = 4 */
    {0x3212, 0x13},   /* end group 3 */
    {0x3212, 0xa3},   /* launch group 3 */
    {REG_DLY, 300},   /* let the sensor stream settle */
};

/* Vendor RGB565_QVGA extra timing (PLL + line/total timing + exposure) - the
 * proven QVGA config from app/ov5640_to_lcd_clone, with the JPEG blocks
 * enabled (0x3002=0x00, 0x3006=0xff) and JPEG mode set (0x4713). */
static const uint16_t ov5640_qvga[][2] = {
    {0x3035, 0x41}, {0x3036, 0x72},   /* PLL (24 MHz input) */
    {0x3c07, 0x08},
    {0x3820, 0x42}, {0x3821, 0x20},   /* flip / mirror + JPEG ENABLE (bit5) */
    {0x3814, 0x31}, {0x3815, 0x31},   /* timing X/Y inc */
    {0x3800, 0x00}, {0x3801, 0x00},   /* HS */
    {0x3802, 0x00}, {0x3803, 0xbe},   /* VS */
    {0x3804, 0x0a}, {0x3805, 0x3f},   /* HW (HE) */
    {0x3806, 0x06}, {0x3807, 0xe4},   /* VH (VE) */
    {0x3808, 0x01}, {0x3809, 0x40},   /* DVPHO = 320 */
    {0x380a, 0x00}, {0x380b, 0xf0},   /* DVPVO = 240 */
    {0x3810, 0x00}, {0x3811, 0x10},   /* H offset = 16 */
    {0x3812, 0x00}, {0x3813, 0x04},   /* V offset = 4 */
    {0x380c, 0x07}, {0x380d, 0x69},   /* HTS */
    {0x380e, 0x03}, {0x380f, 0x21},   /* VTS */
    {0x3618, 0x00}, {0x3612, 0x29}, {0x3709, 0x52}, {0x370c, 0x03},
    {0x3a02, 0x09}, {0x3a03, 0x63},   /* 60Hz max exposure */
    {0x3a14, 0x09}, {0x3a15, 0x63},   /* 50Hz max exposure */
    {0x4004, 0x02},                   /* BLC line number */
    {0x3002, 0x00},                   /* enable JPEG block (was 0x1c)      */
    {0x3006, 0xff},                   /* enable JPEG clocks (was 0xc3)     */
    {0x4713, 0x02},                   /* JPEG mode 2 (was 0x03 RGB565)     */
    {0x4407, 0x04}, {0x460b, 0x35}, {0x460c, 0x22},
    {0x4837, 0x22},                   /* MIPI global timing */
    {0x3824, 0x02},                   /* PCLK manual divider */
    {0x5001, 0xa3},                   /* SDE on, CMX on, AWB on */
    {0x3503, 0x00},                   /* AEC/AGC on */
};

/* JPEG output override table (re-applied by the soft restart): registers the
 * JPEG-enable list verified by app/jpeg_test on this module. */
static const uint16_t ov5640_jpeg_fmt[][2] = {
    {0x3820, 0x40},   /* vertical flip */
    {0x3821, 0x20},   /* JPEG/COMPRESSION ENABLE (bit5) - the crucial bit */
    {0x4713, 0x02},   /* JPEG mode 2 */
    {0x4300, 0x00},   /* YUV output (JPEG routed via YUV) */
    {0x501f, 0x30},   /* YUYV */
    {0x3002, 0x00},   /* enable JPEG block */
    {0x3006, 0xff},   /* enable JPEG clocks */
    {0x471c, 0x50},   /* JPEG mode / quant */
};

/* --- JPEG ring buffer + DMA ------------------------------------------------ */
#define RING_WORDS  (OV5640_JPEG_RING_SIZE / 4U)   /* 8192 words (<= 0xFFFF) */

static uint8_t  snap_buf[OV5640_JPEG_RING_SIZE] __attribute__((section(".sram_dma"), used));
static volatile int camera_ready;
static volatile uint8_t OV5640_FrameState;   /* set by the FRAME callback */

/* The HAL handles live in internal SRAM (.sram_dma): this app's .bss sits in
 * external SDRAM (DATA_IN_ExtSDRAM), and the DCMI/DMA interrupt paths touch
 * these structs from IRQ context while the main loop re-arms the capture -
 * keep them in fast, DMA-safe SRAM like the proven clone app. */
static DCMI_HandleTypeDef hdcmi __attribute__((section(".sram_dma"), used));
static DMA_HandleTypeDef  hdma_dcmi __attribute__((section(".sram_dma"), used));

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
    OV5640_FrameState = 1;      /* the ring advanced (JPEG frame boundary) */
}

/* The HAL aborts the DMA on a DCMI sync error or FIFO overflow - capture
 * would silently stop. Record it; the health watchdog restarts capture
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

static int dcmi_started;           /* non-zero once DCMI+DMA capture runs */

/* Arm the CONTINUOUS DCMI + CIRCULAR DMA ring capture. A clean
 * HAL_DCMI_Stop clears the DCMI state + FIFO overrun before the next
 * capture, so CAPTURE is only re-enabled by Start_DMA AFTER the DMA is
 * armed - otherwise the DCMI captures into the FIFO without a DMA and
 * overruns (RISR OVRRI), losing the frame and stalling the re-arm. */
static HAL_StatusTypeDef ov5640_capture_arm(void)
{
    HAL_StatusTypeDef st;
    HAL_DCMI_Stop(&hdcmi);
    dcmi_dma_init();
    st = HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_CONTINUOUS,
                            (uint32_t)snap_buf, RING_WORDS);
    dcmi_started = 1;
    return st;
}

/* Stop the capture entirely. */
static void ov5640_capture_stop(void)
{
    if (dcmi_started)
    {
        HAL_DCMI_Stop(&hdcmi);
        HAL_DMA_Abort(&hdma_dcmi);
        dcmi_started = 0;
    }
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
    hdma_dcmi.Init.Mode                = DMA_CIRCULAR;   /* JPEG ring: wraps */
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
    hdcmi.Init.JPEGMode         = DCMI_JPEG_ENABLE;   /* 8-bit JPEG stream */
    HAL_DCMI_Init(&hdcmi);

    /* Start the CONTINUOUS DCMI + CIRCULAR DMA ring. */
    ov5640_capture_arm();
}

/* --- Frame hand-out (JPEG ring) ------------------------------------------- */

/* Linear staging buffer: the newest complete JPEG frame is copied here so
 * the pointer returned by OV5640_GetFrame is stable until the next call and
 * a frame straddling the ring wrap (SOI before EOI across the ring end) is
 * re-assembled contiguously. Lives in .bss (SDRAM on this board) - it is
 * only read by the CPU after the copy completes (the DMA never touches it),
 * and the HTTP layer sends it with TCP_WRITE_FLAG_COPY. */
#define OV5640_MIN_JPEG_LEN  500U     /* reject stray FF D8 / FF D9 noise */
static uint8_t jpg_out[OV5640_JPEG_RING_SIZE];

static volatile uint32_t g_last_frame_tick;   /* HAL_GetTick() of last frame */
static volatile uint32_t g_frame_count;       /* frames handed out since boot */
static volatile uint32_t g_last_get_tick;     /* last GetFrame request       */

/* Scan the whole ring for complete JPEG frames (FF D8 FF ... FF D9) and
 * copy the LAST one found into out[]. Returns the length (> 0) or 0.
 * The proven pattern from the earlier MJPEG app (commit c37de5b) - a full
 * 32 KB scan is ~0.1 ms at 180 MHz, far cheaper than the DMA frame period. */
static uint32_t jpeg_copy_latest_frame(uint8_t *out)
{
    uint32_t size = OV5640_JPEG_RING_SIZE;
    uint32_t best_off = 0, best_len = 0;
    uint32_t i = 0;

    while (i + 2 < size)
    {
        if (snap_buf[i] == 0xFF && snap_buf[i + 1] == 0xD8 &&
            snap_buf[i + 2] == 0xFF)
        {
            uint32_t j = i + 2;
            while (j + 1 < size)
            {
                if (snap_buf[j] == 0xFF && snap_buf[j + 1] == 0xD9)
                {
                    uint32_t flen = (j + 2) - i;
                    if (flen >= OV5640_MIN_JPEG_LEN)
                    {
                        best_off = i;
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

    if (best_len)
    {
        memcpy(out, &snap_buf[best_off], best_len);
        return best_len;
    }
    return 0;
}

/* Hand out the latest complete JPEG frame from the ring. Returns 1 and sets
 * *frame (points into jpg_out, valid until the next call) and *len, or 0 if
 * no complete frame is available yet. */
int OV5640_GetFrame(const uint8_t **frame, uint32_t *len)
{
    uint32_t flen;

    g_last_get_tick = HAL_GetTick();

    /* Continuous mode fires the FRAME callback (per ring wrap / VSYNC as
     * the HAL re-enables the FRAME interrupt); if nothing advanced since
     * the last call, don't rescan the same data. */
    if (!OV5640_FrameState)
    {
        return 0;
    }
    OV5640_FrameState = 0;

    flen = jpeg_copy_latest_frame(jpg_out);
    if (flen == 0)
    {
        return 0;
    }

    *frame = jpg_out;
    *len = flen;
    g_last_frame_tick = HAL_GetTick();
    g_frame_count++;

    /* CONTINUOUS capture: no re-arm needed; the DMA keeps writing. */
    return 1;
}

uint32_t OV5640_FrameCount(void)
{
    return g_frame_count;
}

/* Health check: the capture only runs while frames are requested (a browser
 * is streaming), so only check while someone is actively polling. If a frame
 * request is pending but no frame arrives for a while, the capture is stuck -
 * re-init (rate-limited). Call periodically from the main loop. */
void OV5640_HealthCheck(void)
{
    uint32_t now = HAL_GetTick();
    static uint32_t last_msg;

    if (!camera_ready) return;

    /* Nobody has requested a frame for 3 s - the sensor is idle, skip. */
    if ((now - g_last_get_tick) > 3000U) return;

    /* A frame arrived recently - all good. */
    if ((now - g_last_frame_tick) < 3000U) return;

    if (now - last_msg >= 10000U)
    {
        printf("OV5640: no frame for 3 s - re-initing\r\n");
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
    uint32_t flen;
    uint32_t frames = 0;
    uint32_t t0 = HAL_GetTick();

    /* Count JPEG frames for 1.5 s. */
    while ((HAL_GetTick() - t0) < 1500U)
    {
        if (OV5640_GetFrame(&frame, &flen)) frames++;
    }

    uint32_t dt = HAL_GetTick() - t0;
    if (frames)
    {
        printf("OV5640: selftest OK - %lu JPEG frames (%ux%u), %lu fps\r\n",
               (unsigned long)frames, (unsigned)OV5640_FRAME_W,
               (unsigned)OV5640_FRAME_H,
               (unsigned long)(frames * 1000U / (dt ? dt : 1U)));
    }
    else
    {
        printf("OV5640: selftest FAILED - no JPEG frames\r\n");
    }
    g_last_get_tick = 0;   /* idle until a client asks for frames */
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
        ov5640_capture_stop();
    }

    /* The handles live in .sram_dma (NOLOAD - not zeroed by startup), so
     * clear them: HAL_DCMI_Init needs State == HAL_DCMI_STATE_RESET to run
     * its MspInit (clocks/GPIO), and a garbage State hangs the boot. */
    memset(&hdcmi, 0, sizeof(hdcmi));
    memset(&hdma_dcmi, 0, sizeof(hdma_dcmi));

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
        /* Configure JPEG QVGA output (vendor base + QVGA + JPEG fmt). */
        ov5640_write_table(ov5640_base_rgb565,
                           sizeof(ov5640_base_rgb565) / sizeof(ov5640_base_rgb565[0]));
        ov5640_write_table(ov5640_qvga,
                           sizeof(ov5640_qvga) / sizeof(ov5640_qvga[0]));
        ov5640_write_table(ov5640_jpeg_fmt,
                           sizeof(ov5640_jpeg_fmt) / sizeof(ov5640_jpeg_fmt[0]));

        /* Bring up DCMI + DMA capture. */
        dcmi_dma_init();
        /* Clear the ring BEFORE capture starts so the frame-verify below
         * only counts fresh sensor data. */
        memset(snap_buf, 0, sizeof(snap_buf));
        OV5640_FrameState = 0;
        dcmi_init();
        dcmi_started = 1;

        /* Wait ~1.5 s for the first complete JPEG frame. */
        dcmi_frame_evts = 0;
        dcmi_vsync_evts = 0;
        g_frame_count = 0;
        g_last_frame_tick = 0;
        g_last_get_tick = HAL_GetTick();
        {
            const uint8_t *f;
            uint32_t flen;
            int got = 0;
            uint32_t t0 = HAL_GetTick();
            while ((HAL_GetTick() - t0) < 1500U)
            {
                if (OV5640_GetFrame(&f, &flen)) { got = 1; break; }
            }
            if (got)
            {
                printf("OV5640: init ok (attempt %d, JPEG %ux%u, "
                       "first frame %lu B)\r\n",
                       attempt, (unsigned)OV5640_FRAME_W,
                       (unsigned)OV5640_FRAME_H, (unsigned long)flen);
                camera_ready = 1;
                g_last_get_tick = 0;   /* idle until a client asks for frames */
                return 0;
            }
        }

        printf("OV5640: init attempt %d: no frames (vsync=%lu ndtr=%lu "
               "ndtr0=%lu), power-cycling...\r\n",
               attempt, (unsigned long)dcmi_vsync_evts,
               (unsigned long)hdma_dcmi.Instance->NDTR,
               (unsigned long)RING_WORDS);
        ov5640_capture_stop();
        ov5640_power_on();
    }

    printf("OV5640: init FAILED after 4 attempts\r\n");
    return -1;
}
