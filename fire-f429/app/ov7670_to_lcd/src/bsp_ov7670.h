#ifndef __OV7670_H
#define	__OV7670_H

/**
  * @file    ov7670_to_lcd/src/bsp_ov7670.h
  * @brief   OV7670 (no FIFO) DCMI camera driver for the fire-f429 board.
  *
  * The ov7670 user module has NO FIFO chip, so the sensor is wired directly
  * to the STM32 DCMI parallel interface (8-bit RGB565) - the same DCMI pin
  * map as the OV5640 template. The module has no crystal either, so PA8
  * (MCO1) drives XCLK from the 25 MHz HSE.
  */

#include "stm32f4xx.h"
#include "stm32f4xx_hal.h"

extern DCMI_HandleTypeDef DCMI_Handle;

/* 摄像头采集图像的大小 - QVGA 320x240 RGB565 (proven stable; VGA 640x480
 * was abandoned - fragile multi-quarter DBM + marginal PCLK timing).
 * The frame = 153600 bytes = 38400 words <= 16-bit DMA NDTR (65535), so
 * the capture uses a SINGLE DMA buffer (the proven OV5640-clone snapshot
 * path - no fragile multi-buffer DBM). */
#define img_width  320
#define img_height 240
#define OV7670_FRAME_BYTES  (img_width * img_height * 2)  /* 153600 */

/* Snap buffer: one QVGA RGB565 frame in SRAM (.bss) - DMA2-accessible,
 * and the LTDC never reads it (the app blits a copy to the display FB). */
extern uint8_t snap_buf[OV7670_FRAME_BYTES];

/* Set to 1 by HAL_DCMI_FrameEventCallback when a full snapshot is in the
 * buffer. The consumer re-arms after blit. */
extern volatile uint8_t OV7670_FrameState;

/* Image Sizes enumeration */
typedef enum
{
  BMP_320x240 = 0x00,   /* BMP Image 320x240 Size */
} ImageFormat_TypeDef;

/* ---------------- OV7670 register addresses (SCCB, 8-bit) ---------------- */
#define OV7670_REG_COM7     0x12   /* Common Control 7: format/reset */
#define OV7670_REG_COM3     0x0C   /* Common Control 3 */
#define OV7670_REG_COM10    0x15   /* Common Control 10 */
#define OV7670_REG_COM14    0x3E   /* Common Control 14 */
#define OV7670_REG_COM15    0x40   /* Common Control 15: RGB565 select */
#define OV7670_REG_MVFP     0x1E   /* Mirror / Vflip */
#define OV7670_REG_CLKRC    0x11   /* Internal Clock */
#define OV7670_REG_HSTART   0x17   /* Horizontal frame start (high 8 bits) */
#define OV7670_REG_HSTOP    0x18   /* Horizontal frame end (high 8 bits) */
#define OV7670_REG_HREF     0x32   /* HREF: low 3 bits of hstart/hstop */
#define OV7670_REG_VSTART   0x19   /* Vertical frame start (high 8 bits) */
#define OV7670_REG_VSTOP    0x1A   /* Vertical frame end (high 8 bits) */
#define OV7670_REG_VREF     0x03   /* VREF: low 2 bits of vstart/vstop */
#define OV7670_REG_SCALING_XSC   0x70
#define OV7670_REG_SCALING_YSC   0x71
#define OV7670_REG_SCALING_DCWCTR 0x72
#define OV7670_REG_SCALING_PCLK_DIV 0x73
#define OV7670_REG_SCALING_PCLK_DELAY 0xA2

/* Product ID: PID = 0x76 @ 0x0A, VER = 0x73 @ 0x0B (ALIENTEK/ESP32). */
#define OV7670_SENSOR_PIDH  0x0A
#define OV7670_SENSOR_PIDL  0x0B

/* ---------------- DCMI / camera control pin map (fire-f429) -------------- */
//VSYNC
#define DCMI_VSYNC_GPIO_PORT        	    GPIOI
#define DCMI_VSYNC_GPIO_CLK_ENABLE()        __HAL_RCC_GPIOI_CLK_ENABLE()
#define DCMI_VSYNC_GPIO_PIN         	    GPIO_PIN_5
#define DCMI_VSYNC_AF			            GPIO_AF13_DCMI
// HSYNC (HREF on the OV7670)
#define DCMI_HSYNC_GPIO_PORT        	    GPIOA
#define DCMI_HSYNC_GPIO_CLK_ENABLE()        __HAL_RCC_GPIOA_CLK_ENABLE()
#define DCMI_HSYNC_GPIO_PIN         	    GPIO_PIN_4
#define DCMI_HSYNC_AF			            GPIO_AF13_DCMI
//PIXCLK
#define DCMI_PIXCLK_GPIO_PORT        	    GPIOA
#define DCMI_PIXCLK_GPIO_CLK_ENABLE()       __HAL_RCC_GPIOA_CLK_ENABLE()
#define DCMI_PIXCLK_GPIO_PIN         	    GPIO_PIN_6
#define DCMI_PIXCLK_AF			            GPIO_AF13_DCMI
//PWDN
#define DCMI_PWDN_GPIO_PORT        	        GPIOG
#define DCMI_PWDN_GPIO_CLK_ENABLE()         __HAL_RCC_GPIOG_CLK_ENABLE()
#define DCMI_PWDN_GPIO_PIN         	        GPIO_PIN_3
//RST
#define DCMI_RST_GPIO_PORT                  GPIOG
#define DCMI_RST_GPIO_CLK_ENABLE()          __HAL_RCC_GPIOG_CLK_ENABLE()
#define DCMI_RST_GPIO_PIN                   GPIO_PIN_2
//数据信号线
#define DCMI_D0_GPIO_PORT        	        GPIOH
#define DCMI_D0_GPIO_CLK_ENABLE()         	__HAL_RCC_GPIOH_CLK_ENABLE()
#define DCMI_D0_GPIO_PIN         	        GPIO_PIN_9
#define DCMI_D0_AF			                GPIO_AF13_DCMI

#define DCMI_D1_GPIO_PORT        	        GPIOH
#define DCMI_D1_GPIO_CLK_ENABLE()         	__HAL_RCC_GPIOH_CLK_ENABLE()
#define DCMI_D1_GPIO_PIN         	        GPIO_PIN_10
#define DCMI_D1_AF			                GPIO_AF13_DCMI

#define DCMI_D2_GPIO_PORT        	        GPIOH
#define DCMI_D2_GPIO_CLK_ENABLE()         	__HAL_RCC_GPIOH_CLK_ENABLE()
#define DCMI_D2_GPIO_PIN         	        GPIO_PIN_11
#define DCMI_D2_AF			                GPIO_AF13_DCMI

#define DCMI_D3_GPIO_PORT        	        GPIOH
#define DCMI_D3_GPIO_CLK_ENABLE()         	__HAL_RCC_GPIOH_CLK_ENABLE()
#define DCMI_D3_GPIO_PIN         	        GPIO_PIN_12
#define DCMI_D3_AF			                GPIO_AF13_DCMI

#define DCMI_D4_GPIO_PORT        	        GPIOH
#define DCMI_D4_GPIO_CLK_ENABLE()         	__HAL_RCC_GPIOH_CLK_ENABLE()
#define DCMI_D4_GPIO_PIN         	        GPIO_PIN_14
#define DCMI_D4_AF			                GPIO_AF13_DCMI

#define DCMI_D5_GPIO_PORT        	        GPIOD
#define DCMI_D5_GPIO_CLK_ENABLE()         	__HAL_RCC_GPIOD_CLK_ENABLE()
#define DCMI_D5_GPIO_PIN         	        GPIO_PIN_3
#define DCMI_D5_AF			                GPIO_AF13_DCMI

#define DCMI_D6_GPIO_PORT        	        GPIOI
#define DCMI_D6_GPIO_CLK_ENABLE()         	__HAL_RCC_GPIOI_CLK_ENABLE()
#define DCMI_D6_GPIO_PIN         	        GPIO_PIN_6
#define DCMI_D6_AF			                GPIO_AF13_DCMI

#define DCMI_D7_GPIO_PORT        	        GPIOI
#define DCMI_D7_GPIO_CLK_ENABLE()         	__HAL_RCC_GPIOI_CLK_ENABLE()
#define DCMI_D7_GPIO_PIN         	        GPIO_PIN_7
#define DCMI_D7_AF			                GPIO_AF13_DCMI

/*debug*/
#define CAMERA_DEBUG_ON          1
#define CAMERA_DEBUG_ARRAY_ON   1
#define CAMERA_DEBUG_FUNC_ON    1

// Log define
#define CAMERA_INFO(fmt,arg...)           printf("<<-CAMERA-INFO->> "fmt"\n",##arg)
#define CAMERA_ERROR(fmt,arg...)          printf("<<-CAMERA-ERROR->> "fmt"\n",##arg)
#define CAMERA_DEBUGF(fmt,arg...)         do{\
                                         if(CAMERA_DEBUG_ON)\
                                         printf("<<-CAMERA-DEBUG->> [%d]"fmt"\n",__LINE__, ##arg);\
                                       }while(0)
#define CAMERA_DEBUG_FUNC()               do{\
                                         if(CAMERA_DEBUG_FUNC_ON)\
                                         printf("<<-CAMERA-FUNC->> Func:%s@Line:%d\n",__func__,__LINE__);\
                                       }while(0)

/* Exported types ------------------------------------------------------------*/
//存储摄像头ID的结构体
typedef struct
{
  uint8_t PIDH;
  uint8_t PIDL;
} OV7670_IDTypeDef;

/* Exported functions --------------------------------------------------------*/
void OV7670_SCCB_MinInit(void);         /* minimal: RST/PWDN outputs + power seq */
void OV7670_DCMI_GpioInit(void);        /* DCMI data/sync pins (AF13)          */
void OV7670_PowerCycle(void);           /* power-cycle, leave running (RST released) */
void OV7670_ReadID(OV7670_IDTypeDef *OV7670ID);
uint8_t OV7670_Config(void);            /* register table + QVGA RGB565, 0 = OK */
void OV7670_Init(void);                 /* DCMI config + DMA snapshot arm */
void OV7670_DMA_Config(uint32_t DMA_Memory0BaseAddr, uint32_t DMA_BufferSize);
void OV7670_DCMI_Resume(void);          /* re-arm snapshot after a frame */
void OV7670_CaptureStop(void);          /* freeze capture (stop + disable FRAME IT) */
void OV7670_XCLK_Init(void);            /* PA8 = MCO1 = HSE/2 */

#endif /* __OV7670_H */