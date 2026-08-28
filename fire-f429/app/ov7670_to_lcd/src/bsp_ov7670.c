/**
  ******************************************************************************
  * @file    bsp_ov7670.c
  * @brief   OV7670 (no FIFO) DCMI camera driver for the fire-f429 board.
  *
  * Adapted for a FIFO-less OV7670 user module:
  *   - no FIFO chip on the module -> the sensor's parallel 8-bit RGB565 is
  *     captured directly by the STM32 DCMI (same pin map / snapshot capture
  *     as app/ov5640_to_lcd_clone),
  *   - no crystal on the module -> PA8 (MCO1) outputs the HSE clock as XCLK
  *     (OV7670_XCLK_Init). The reference ALIENTEK design drove XCLK with a
  *     12 MHz timer PWM; this board uses MCO1 instead (see OV7670_XCLK_Init
  *     for the divider choice).
  *
  * Register table: the ALIENTEK OV7670 QVGA RGB565 table (verified to
  * produce a 320x240 RGB565 stream - the FIFO only buffered that stream; the
  * sensor configuration itself is FIFO-agnostic). SCCB via bsp_i2c.c/I2C1.
  ******************************************************************************
  */
#include "bsp_ov7670.h"
#include "bsp_i2c.h"

DCMI_HandleTypeDef DCMI_Handle;
DMA_HandleTypeDef DMA_Handle_dcmi;

#define TIMEOUT  2

ImageFormat_TypeDef ImageFormat;

/* ------------------------------------------------------------------ */
/* QVGA capture buffer: one 320x240 RGB565 frame = 153600 bytes in SRAM
 * (.bss) - DMA2-accessible, and the LTDC never reads it (the app blits a
 * copy to the display framebuffer). The frame fits ONE DMA buffer
 * (38400 words <= 0xFFFF NDTR), so the capture uses the proven single-
 * buffer snapshot path (no fragile multi-buffer DBM). */
__attribute__((aligned(32))) uint8_t snap_buf[OV7670_FRAME_BYTES];

volatile uint8_t OV7670_FrameState = 0;   /* set by HAL_DCMI_FrameEventCallback */

/* ------------------------------------------------------------------ */
/* OV7670 QVGA (320x240) RGB565 register table.
 *
 * BASE: the no-FIFO recipe from iwatake2222/DigitalCamera_STM32
 * (ov7670Reg.h) + the OpenMV/esp32-camera QVGA frame-control values
 * (ov7670_frame_control(158,14,10,490) + QVGA scaling):
 *   - COM7=0x14 (QVGA RGB565), RGB444 off (0x8C=0x00), COM15=0xD0 full range
 *   - COM3=0x04 (DCW), COM14=0x19 (manual scaling, PCLK/2),
 *     XSC/YSC/DCWCTR/PCLK_DIV = 0x3A/0x35/0x11/0xF1 (proven no-FIFO QVGA)
 *   - window HSTART/HSTOP/HREF = 158>>3, 14>>3, (14&7)<<3|(158&7)
 *     VSTART/VSTOP/VREF = 10>>2, 490>>2, ((490&2)<<2)|(10&2)
 *   - COM13=0x80 (gamma enable + UV auto), COM16=0x38 (edge/de-noise/AWB)
 *   - MVFP=0x31 (mirror+flip, sensor orientation on this connector),
 *   - AGC/AEC/AWB auto blocks + gamma curve from ALIENTEK (kept).
 */
static const uint8_t ov7670_init_reg_tbl[][2] =
{
    /* format */
    {0x12, 0x14},   /* COM7: QVGA (320x240), RGB565 */
    {0x8c, 0x00},   /* RGB444 disable */
    {0x40, 0xd0},   /* COM15: RGB565, 00-FF (full range) */
    {0x3a, 0x0c},   /* TSLB: UYVY-style output (reference) */

    /* window / scaling (QVGA no-FIFO recipe) */
    {0x0c, 0x04},   /* COM3: DCW enable */
    {0x3e, 0x19},   /* COM14: manual scaling, PCLK/2 */
    {0x70, 0x3a},   /* SCALING_XSC */
    {0x71, 0x35},   /* SCALING_YSC */
    {0x72, 0x11},   /* SCALING_DCWCTR: down sample by 2 */
    {0x73, 0xf1},   /* SCALING_PCLK_DIV: DSP clock /2 */
    {0xa2, 0x02},   /* SCALING_PCLK_DELAY */
    {0x32, 0x80},   /* HREF */
    {0x17, 0x16},   /* HSTART */
    {0x18, 0x04},   /* HSTOP */
    {0x19, 0x03},   /* VSTART = 14 (3*4+2) */
    {0x1a, 0x7b},   /* VSTOP = 494 (123*4+2) */
    {0x03, 0x0a},   /* VREF */
    {0x15, 0x00},   /* COM10 */
    {0x11, 0x00},   /* CLKRC: pre-scalar 1/1 */

    /* gamma enable + output */
    {0x3d, 0x80},   /* COM13: gamma enable, UV auto adjust */
    {0x41, 0x38},   /* COM16: edge enhance, de-noise, AWB gain */

    /* gamma curve (ALIENTEK values) */
    {0x7a, 0x20}, {0x7b, 0x1c}, {0x7c, 0x28},
    {0x7d, 0x3c}, {0x7e, 0x55}, {0x7f, 0x68},
    {0x80, 0x76}, {0x81, 0x80}, {0x82, 0x88},
    {0x83, 0x8f}, {0x84, 0x96}, {0x85, 0xa3},
    {0x86, 0xaf}, {0x87, 0xc4}, {0x88, 0xd7},
    {0x89, 0xe8},

    /* AGC / AEC (auto) */
    {0x13, 0xe0},   /* COM8 */
    {0x00, 0x00},   /* GAIN */
    {0x10, 0x00},   /* AEC */
    {0x0d, 0x00},   /* COM4 */
    {0x14, 0x20},   /* COM9: limit the max gain */
    {0xa5, 0x05},   /* BD50MAX */
    {0xab, 0x07},   /* BD60MAX */
    {0x24, 0x75},   /* AEW */
    {0x25, 0x63},   /* AEB */
    {0x26, 0xa5},   /* VPT */
    {0x9f, 0x78},   /* HAECC1 */
    {0xa0, 0x68},   /* HAECC2 */
    {0xa1, 0x03},   /* A1 */
    {0xa6, 0xdf},   /* HAECC3 */
    {0xa7, 0xdf},   /* HAECC4 */
    {0xa8, 0xf0},   /* HAECC5 */
    {0xa9, 0x90},   /* HAECC6 */
    {0xaa, 0x94},   /* HAECC7 */
    {0x13, 0xe5},   /* COM8 */
    {0x0e, 0x61},   /* COM5 */
    {0x0f, 0x4b},   /* COM6 */
    {0x16, 0x02},   /* RSVD_16 */
    {0x1e, 0x31},   /* MVFP: mirror + flip (sensor orientation) */
    {0x21, 0x02},   /* ADCCTR1 */
    {0x22, 0x91},   /* ADCCTR2 */
    {0x29, 0x07},   /* RSVD_29 */
    {0x33, 0x0b},   /* CHLF */
    {0x35, 0x0b},   /* RSVD_35 */
    {0x37, 0x1d},   /* ADC */
    {0x38, 0x71},   /* ACOM */
    {0x39, 0x2a},   /* OFON */
    {0x3c, 0x78},   /* COM12 */
    {0x4d, 0x40},   /* RSVD_4D */
    {0x4e, 0x20},   /* RSVD_4E */
    {0x69, 0x5d},   /* GFIX */
    {0x6b, 0x40},   /* DBLV: PLL *4 (12.5 MHz XCLK -> ~50 MHz internal) */
    {0x74, 0x19},   /* REG74 */
    {0x8d, 0x4f},   /* RSVD_8D */
    {0x8e, 0x00}, {0x8f, 0x00}, {0x90, 0x00}, {0x91, 0x00},
    {0x92, 0x00}, {0x96, 0x00}, {0x9a, 0x80},
    {0xb0, 0x84}, {0xb1, 0x0c}, {0xb2, 0x0e}, {0xb3, 0x82},
    {0xb8, 0x0a},   /* RSVD_B8 */

    /* AWB */
    {0x43, 0x14}, {0x44, 0xf0}, {0x45, 0x34}, {0x46, 0x58},
    {0x47, 0x28}, {0x48, 0x3a},
    {0x59, 0x88}, {0x5a, 0x88}, {0x5b, 0x44}, {0x5c, 0x67},
    {0x5d, 0x49}, {0x5e, 0x0e},

    /* lens correction */
    {0x64, 0x04}, {0x65, 0x20}, {0x66, 0x05},
    {0x94, 0x04}, {0x95, 0x08},
    {0x6c, 0x0a}, {0x6d, 0x55},   /* AWBCTR3/2 */

    /* color matrix (default coeff set) */
    {0x4f, 0x80}, {0x50, 0x80}, {0x51, 0x00}, {0x52, 0x22},
    {0x53, 0x5e}, {0x54, 0x80},
    {0x09, 0x03},   /* COM2 */
    {0x6e, 0x11}, {0x6f, 0x9f},   /* AWBCTR1/0 */

    /* brightness / contrast / center (neutral values) */
    {0x55, 0x00},   /* BRIGHTNESS (0 = neutral) */
    {0x56, 0x40},   /* CONTRAST   (2 = neutral) */
    {0x57, 0x80},   /* CONTRASCENTER */
};

/* ------------------------------------------------------------------ */

/**
  * @brief  PA8 (MCO1) outputs the HSE clock to the OV7670 XCLK.
  *
  * The ov7670 module has NO clock source of its own (unlike the ov5640
  * module's 24 MHz crystal), so the MCU must drive XCLK. MCO1 is used -
  * just the MCO output, no timer PWM.
  *
  * Divider: the ALIENTEK register table is designed around a 12 MHz XCLK
  * (their board used a 12 MHz timer PWM; DBLV PLL x4 -> ~48 MHz internal).
  * The fire-f429 HSE is 25 MHz, so MCO1 = HSE/2 = 12.5 MHz keeps the
  * register table's clock assumptions. 25 MHz (RCC_MCODIV_1) is inside the
  * OV7670 10-48 MHz spec too - to use it, change the divider and re-tune
  * CLKRC (0x11) / DBLV (0x6B) in the table above.
  */
void OV7670_XCLK_Init(void)
{
    HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_HSE, RCC_MCODIV_2); /* PA8 */
}

/**
  * @brief  Minimal SCCB bring-up pins + power cycle.
  *
  * Step-1 (verified by app/ov7670_sccb_test): configure ONLY RST (PG2) and
  * PWDN (PG3) as outputs, then power the sensor EXACTLY like the test did:
  *   PWDN = low (power ON, never raised),
  *   RST  = low for 20 ms, then released high,
  *   100 ms settle before the SCCB answers.
  * Crucially, the DCMI data/sync pins are NOT touched yet - the OV7670's
  * SCCB was proven to answer only in this minimal configuration.
  */
void OV7670_SCCB_MinInit(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* RST PG2, PWDN PG3: push-pull outputs */
    gpio.Pin = DCMI_RST_GPIO_PIN | DCMI_PWDN_GPIO_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOG, &gpio);

    /* power cycle (proven combo: RST released L2H, PWDN low) */
    HAL_GPIO_WritePin(DCMI_RST_GPIO_PORT, DCMI_RST_GPIO_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(DCMI_PWDN_GPIO_PORT, DCMI_PWDN_GPIO_PIN, GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(DCMI_RST_GPIO_PORT, DCMI_RST_GPIO_PIN, GPIO_PIN_SET);
    HAL_Delay(100);
}

/**
  * @brief  初始化控制摄像头使用的GPIO(DCMI) - data/sync pins only.
  * @retval None
  */
void OV7670_DCMI_GpioInit(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    /***DCMI引脚配置***/
    DCMI_PWDN_GPIO_CLK_ENABLE();
    DCMI_RST_GPIO_CLK_ENABLE();
    DCMI_VSYNC_GPIO_CLK_ENABLE();
    DCMI_HSYNC_GPIO_CLK_ENABLE();
    DCMI_PIXCLK_GPIO_CLK_ENABLE();
    DCMI_D0_GPIO_CLK_ENABLE();
    DCMI_D1_GPIO_CLK_ENABLE();
    DCMI_D2_GPIO_CLK_ENABLE();
    DCMI_D3_GPIO_CLK_ENABLE();
    DCMI_D4_GPIO_CLK_ENABLE();
    DCMI_D5_GPIO_CLK_ENABLE();
    DCMI_D6_GPIO_CLK_ENABLE();
    DCMI_D7_GPIO_CLK_ENABLE();

    /*控制/同步信号线*/
    GPIO_InitStructure.Pin = DCMI_VSYNC_GPIO_PIN;
    GPIO_InitStructure.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStructure.Speed = GPIO_SPEED_HIGH;
    GPIO_InitStructure.Pull = GPIO_PULLUP;
    GPIO_InitStructure.Alternate = DCMI_VSYNC_AF;
    HAL_GPIO_Init(DCMI_VSYNC_GPIO_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.Pin = DCMI_HSYNC_GPIO_PIN;
    GPIO_InitStructure.Alternate = DCMI_HSYNC_AF;
    HAL_GPIO_Init(DCMI_HSYNC_GPIO_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.Pin = DCMI_PIXCLK_GPIO_PIN;
    GPIO_InitStructure.Alternate = DCMI_PIXCLK_AF;
    HAL_GPIO_Init(DCMI_PIXCLK_GPIO_PORT, &GPIO_InitStructure);

    /*数据信号*/
    GPIO_InitStructure.Pin = DCMI_D0_GPIO_PIN;
    GPIO_InitStructure.Alternate = DCMI_D0_AF;
    HAL_GPIO_Init(DCMI_D0_GPIO_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.Pin = DCMI_D1_GPIO_PIN;
    GPIO_InitStructure.Alternate = DCMI_D1_AF;
    HAL_GPIO_Init(DCMI_D1_GPIO_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.Pin = DCMI_D2_GPIO_PIN;
    GPIO_InitStructure.Alternate = DCMI_D2_AF;
    HAL_GPIO_Init(DCMI_D2_GPIO_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.Pin = DCMI_D3_GPIO_PIN;
    GPIO_InitStructure.Alternate = DCMI_D3_AF;
    HAL_GPIO_Init(DCMI_D3_GPIO_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.Pin = DCMI_D4_GPIO_PIN;
    GPIO_InitStructure.Alternate = DCMI_D4_AF;
    HAL_GPIO_Init(DCMI_D4_GPIO_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.Pin = DCMI_D5_GPIO_PIN;
    GPIO_InitStructure.Alternate = DCMI_D5_AF;
    HAL_GPIO_Init(DCMI_D5_GPIO_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.Pin = DCMI_D6_GPIO_PIN;
    GPIO_InitStructure.Alternate = DCMI_D6_AF;
    HAL_GPIO_Init(DCMI_D6_GPIO_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.Pin = DCMI_D7_GPIO_PIN;
    GPIO_InitStructure.Alternate = DCMI_D7_AF;
    HAL_GPIO_Init(DCMI_D7_GPIO_PORT, &GPIO_InitStructure);
}

/**
  * @brief  Power-cycle the sensor and leave it running (RST released,
  *         PWDN low). Used by the ID retry loop - the OV7670 startup is
  *         flaky, same as the OV5640 bring-up; a fresh power cycle
  *         usually recovers it.
  */
void OV7670_PowerCycle(void)
{
    HAL_GPIO_WritePin(DCMI_RST_GPIO_PORT, DCMI_RST_GPIO_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(DCMI_PWDN_GPIO_PORT, DCMI_PWDN_GPIO_PIN, GPIO_PIN_RESET);
    Delay(20);
    HAL_GPIO_WritePin(DCMI_RST_GPIO_PORT, DCMI_RST_GPIO_PIN, GPIO_PIN_SET);
    Delay(100);
}

/**
  * @brief  软件复位 OV7670
  */
void OV7670_Reset(void)
{
    OV7670_WriteReg(OV7670_REG_COM7, 0x80);
    Delay(50);
}

/**
  * @brief  读取摄像头的ID (PID=0x76 @ 0x0A, VER=0x73 @ 0x0B)
  * @param  OV7670ID: 存储ID的结构体
  */
void OV7670_ReadID(OV7670_IDTypeDef *OV7670ID)
{
    OV7670ID->PIDH = OV7670_ReadReg(OV7670_SENSOR_PIDH);
    OV7670ID->PIDL = OV7670_ReadReg(OV7670_SENSOR_PIDL);
}

/**
  * @brief  写入寄存器配置表 (QVGA RGB565) + readback of the key registers.
  * @retval 0 = OK, 1 = reset failed, 2 = table write failed
  */
uint8_t OV7670_Config(void)
{
    uint32_t i;

    /* 软件复位 (bit-bang SCCB) */
    if (OV7670_WriteReg(OV7670_REG_COM7, 0x80) != 0)
    {
        printf("OV7670 soft reset FAILED (no ACK)\r\n");
        return 1;
    }
    Delay(50);

    /* 写入寄存器配置 (ALIENTEK QVGA RGB565 table) */
    for (i = 0; i < (sizeof(ov7670_init_reg_tbl) / sizeof(ov7670_init_reg_tbl[0])); i++)
    {
        if (OV7670_WriteReg(ov7670_init_reg_tbl[i][0],
                            ov7670_init_reg_tbl[i][1]) != 0)
        {
            printf("  register table write FAILED at entry %lu\r\n",
                   (unsigned long)i);
            return 2;
        }
        Delay(2);
    }

    /* FULL-RANGE RGB565 fix (proven by app/ov7670_dcmi_probe phase A/B:
     * the ALIENTEK table leaves COM15=0x10 = RGB565 with LIMITED output
     * range 0x10..0xF0 -> dark, washed "color curve" image. Writing
     * COM15=0xD0 (RGB565 + full range 00..FF) then rewriting CLKRC last
     * (OpenMV's fix) gives exact 0xFFFF/0x0000 color-bar values.) */
    if (OV7670_WriteReg(OV7670_REG_COM15, 0xD0) != 0) return 3;   /* full range */
    OV7670_WriteReg(OV7670_REG_CLKRC, 0x00);                      /* rewrite CLKRC */

    /* Read back the key registers to confirm the config took:
     * COM7=0x14 (QVGA RGB), COM15=0xD0 (RGB565 full range), PID=0x76. */
    printf("readback: COM7=0x%02x COM15=0x%02x PID=0x%02x VER=0x%02x\r\n",
           (unsigned)OV7670_ReadReg(OV7670_REG_COM7),
           (unsigned)OV7670_ReadReg(OV7670_REG_COM15),
           (unsigned)OV7670_ReadReg(OV7670_SENSOR_PIDH),
           (unsigned)OV7670_ReadReg(OV7670_SENSOR_PIDL));
    return 0;
}

/**
  * @brief  配置 DCMI/DMA 以捕获摄像头数据 (快照模式)
  */
void OV7670_Init(void)
{
    /* 使能DCMI时钟 */
    __HAL_RCC_DCMI_CLK_ENABLE();

    /* DCMI 配置
     * Empirically determined by app/ov7670_dcmi_probe (color-bar + polarity
     * sweep):
     *   VSYNC = HIGH, HREF = LOW, PCLK = RISING (QVGA).
     * The probe measured ~60% more valid color-bar words with PCKPOL=RISING
     * at QVGA's slow pixel rate (the OV5640-clone setting). FALLING was
     * only needed at VGA's 2x pixel rate where the sensor's data setup/hold
     * is marginal on the rising edge. */
    DCMI_Handle.Instance              = DCMI;
    DCMI_Handle.Init.SynchroMode      = DCMI_SYNCHRO_HARDWARE;
    DCMI_Handle.Init.PCKPolarity      = DCMI_PCKPOLARITY_RISING;  /* QVGA-probe */
    DCMI_Handle.Init.VSPolarity       = DCMI_VSPOLARITY_HIGH;
    DCMI_Handle.Init.HSPolarity       = DCMI_HSPOLARITY_LOW;   /* probe-proven */
    DCMI_Handle.Init.CaptureRate      = DCMI_CR_ALL_FRAME;
    DCMI_Handle.Init.ExtendedDataMode = DCMI_EXTEND_DATA_8B;
    HAL_DCMI_Init(&DCMI_Handle);

    /* 配置中断 */
    HAL_NVIC_SetPriority(DCMI_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DCMI_IRQn);

    /* dma_memory 以16位数据为单位， dma_bufsize以32位数据为单位(即像素个数/2) */
    OV7670_DMA_Config((uint32_t)snap_buf, img_width * img_height / 2);
}

/**
  * @brief  配置 DCMI/DMA 以捕获 QVGA 帧 (single-buffer snapshot).
  *
  * QVGA 320x240 RGB565 = 153600 B = 38400 words <= 0xFFFF NDTR, so the
  * capture uses ONE DMA buffer (the proven OV5640-clone snapshot path):
  *   - DCMI SNAPSHOT mode + DMA NORMAL: captures ONE complete frame per
  *     transfer, then the DMA stops.
  *   - The main loop blits the frozen buffer and re-arms the next snapshot
  *     (OV7670_DMA_Config). Deterministic - every blit reads a complete
  *     frame (no rolling ring, no tearing, no phase issues).
  */
/**
  * @brief  配置 DCMI/DMA 以捕获 QVGA 帧 (single-buffer snapshot).
  *         Re-arms the capture; call once at init and after each frame.
  * @param  DMA_Memory0BaseAddr: base of the frame buffer (snap_buf)
  * @param  DMA_BufferSize: total frame in words (38400)
  *
  * The proven commit-4890139 flow: DMA_NORMAL + DCMI_MODE_SNAPSHOT, one
  * frame per transfer. The HAL's DCMI_DMAXferCplt re-enables the FRAME IT
  * only AFTER the DMA completes, so the frame event can never fire before
  * the buffer is full - no abort/EN-guard/flag-clearing needed on re-arm.
  */
void OV7670_DMA_Config(uint32_t DMA_Memory0BaseAddr, uint32_t DMA_BufferSize)
{
    /* 配置DMA从DCMI中获取数据 */
    __HAL_RCC_DMA2_CLK_ENABLE();
    DMA_Handle_dcmi.Instance = DMA2_Stream1;
    DMA_Handle_dcmi.Init.Channel = DMA_CHANNEL_1;
    DMA_Handle_dcmi.Init.Direction = DMA_PERIPH_TO_MEMORY;
    DMA_Handle_dcmi.Init.PeriphInc = DMA_PINC_DISABLE;
    DMA_Handle_dcmi.Init.MemInc = DMA_MINC_ENABLE;
    DMA_Handle_dcmi.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    DMA_Handle_dcmi.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    DMA_Handle_dcmi.Init.Mode = DMA_NORMAL;   /* snapshot: stop after frame */
    DMA_Handle_dcmi.Init.Priority = DMA_PRIORITY_HIGH;
    DMA_Handle_dcmi.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
    DMA_Handle_dcmi.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
    DMA_Handle_dcmi.Init.MemBurst = DMA_MBURST_INC4;
    DMA_Handle_dcmi.Init.PeriphBurst = DMA_PBURST_SINGLE;

    __HAL_LINKDMA(&DCMI_Handle, DMA_Handle, DMA_Handle_dcmi);
    HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream1_IRQn);
    HAL_DMA_Init(&DMA_Handle_dcmi);

    /* 使能DCMI采集数据（快照模式，一帧完成后自动停止） */
    HAL_DCMI_Start_DMA(&DCMI_Handle, DCMI_MODE_SNAPSHOT,
                       (uint32_t)DMA_Memory0BaseAddr, DMA_BufferSize);
}

/**
  * @brief  Freeze the capture: stop the DCMI + disable the FRAME IT so the
  *         buffer is stable (used by the boot pattern phase). The live path
  *         does NOT need this - snapshot mode already stops after one frame.
  */
void OV7670_CaptureStop(void)
{
    HAL_DCMI_Stop(&DCMI_Handle);
    __HAL_DCMI_DISABLE_IT(&DCMI_Handle, DCMI_IT_FRAME);
}

/**
  * @brief  Resume the DCMI after a snapshot transfer (the HAL clears
  *         CAPTURE in snapshot mode). Call before starting the next snapshot.
  */
void OV7670_DCMI_Resume(void)
{
    DCMI_Handle.State = HAL_DCMI_STATE_BUSY;        /* update the DCMI state */
    DCMI_Handle.Instance->CR |= DCMI_CR_CAPTURE;    /* enable DCMI capture   */
}

extern uint8_t fps;
/**
  * @brief  VSYNC event callback - one counter per sensor frame.
  */
void HAL_DCMI_VsyncEventCallback(DCMI_HandleTypeDef *hdcmi)
{
    fps++;
    (void)hdcmi;
}

/**
  * @brief  Frame event callback - a complete snapshot frame is in snap_buf.
  *         The main loop blits it to the display and re-arms the capture.
  *         (The HAL's DCMI_DMAXferCplt re-enables the FRAME IT after the
  *         DMA completes - do NOT re-enable it here.)
  */
void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *hdcmi)
{
    OV7670_FrameState = 1;
    (void)hdcmi;
}

/* --- IRQ handlers ----------------------------------------------------------
 * The repo's startup_stm32f429xx.s maps DCMI_IRQHandler and
 * DMA2_Stream1_IRQHandler to Default_Handler (an infinite loop) unless the
 * app defines them - define both here (same as the proven OV5640 clone).
 *
 * The vendored HAL_DCMI_IRQHandler aborts the DMA on a sync/overflow error,
 * so the error flags are cleared here before the HAL sees them. */
void DCMI_IRQHandler(void)
{
    if (DCMI->RISR & DCMI_FLAG_ERRRI)
    {
        __HAL_DCMI_CLEAR_FLAG(&DCMI_Handle, DCMI_FLAG_ERRRI);
    }
    if (DCMI->RISR & DCMI_FLAG_OVRRI)
    {
        __HAL_DCMI_CLEAR_FLAG(&DCMI_Handle, DCMI_FLAG_OVRRI);
    }
    HAL_DCMI_IRQHandler(&DCMI_Handle);
}

void DMA2_Stream1_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&DMA_Handle_dcmi);
}
/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/