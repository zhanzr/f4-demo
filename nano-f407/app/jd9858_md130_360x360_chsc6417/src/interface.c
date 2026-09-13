/*
  interface.c - FSMC bus primitives for the JD9858 panel (nano-f407).

  The module is wired like the vendor's F103VET6 example: NE1 (PD7) =
  LCD-CS, A16 (PD11) = LCD-DC, NOE (PD4) = LCD-RD, NWE (PD5) = LCD-WR,
  D0..D7 on PD14/PD15/PD0/PD1/PE7/PE8/PE9/PE10 (8-bit data bus). The
  F407VET6 maps the FSMC signals to the same port pins, so the vendor
  addressing carries over unchanged:

    command (DC low): byte write to 0x6000_0000
    data    (DC high): byte write to 0x6001_0000

  Controller config follows the vendor HAL settings (STM32_TK0013F1327
  example, HAL_SRAM_Init): NE1, NOR flash type, 8-bit, access mode B,
  ADDSET = 2, DATAST = 5.
*/

#include "interface.h"
#include "lcd.h"
#include "stm32f4xx_hal.h"

/* DC(RS) is decoded from address bit A16 (NE1 space, 8-bit bus). */
static volatile uint8_t *const LCD_REG8 = (volatile uint8_t *)0x60000000U;
static volatile uint8_t *const LCD_DAT8 = (volatile uint8_t *)0x60010000U;

static SRAM_HandleTypeDef s_sram;
static FSMC_NORSRAM_TimingTypeDef s_timing;
/* DATAST = 4: 16.8 MHz write rate - verified clean on this module.
 * (5 = 15.3 MHz also worked; 3 = 18.7 MHz garbles, 2 = 21 MHz blank.) */
static uint8_t s_datast = 4U;

/* Register-level direct writes: the FSMC drives CS/DC/RD/WR per bus
 * access, so one store = one panel transaction. */
void WriteComm(uint16_t data)
{
    *LCD_REG8 = (uint8_t)data;
}
void WriteData(uint16_t data)
{
    *LCD_DAT8 = (uint8_t)data;
}
void SendData(uint32_t color)
{
    *LCD_DAT8 = (uint8_t)(color >> 8);
    *LCD_DAT8 = (uint8_t)color;
}
void LCD_WriteDataFast(uint8_t data)
{
    *LCD_DAT8 = data;
}

/* Solid-color bulk burst: plain stores, the FSMC paces the bus. */
void LCD_FillBulk(uint32_t color, uint32_t pixels)
{
    volatile uint8_t *d = LCD_DAT8;
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)color;

    while (pixels-- != 0U)
    {
        *d = hi;
        *d = lo;
    }
}

/* CS/DC are hardware-decoded on this bus; framing helpers are no-ops
 * kept for source compatibility with the SPI projects. */
void LCD_BeginData(void) { }
void LCD_EndData(void) { }

/* Effective panel write rate in kHz (for the info page): one 8-bit
 * store = (ADDSET + 1 + DATAST + 1 + ~2) HCLK cycles on this async
 * NOR mapping. */
#define FSMC_ADDSET_HCLK 2U
unsigned long LCD_FsmcKHz(void)
{
    uint32_t cyc = FSMC_ADDSET_HCLK + 1U + s_datast + 1U + 2U;
    return (unsigned long)(HAL_RCC_GetHCLKFreq() / cyc / 1000U);
}

/* GPIO + FSMC controller bring-up (vendor LCD_GPIO_Config +
 * LCD_FSMC_Config, ported to the F4 HAL). Call before LCD_Init. */
void LCD_UseHwBus(void)
{
    GPIO_InitTypeDef g;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_FSMC_CLK_ENABLE();

    /* Data bus D0..D7: PD14, PD15, PD0, PD1, PE7..PE10 on AF12. */
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_LOW;   /* soft edges: the round
                                            module's FPC hates fast
                                            slew on D0..D7 */
    g.Alternate = GPIO_AF12_FSMC;
    g.Pin       = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &g);
    g.Pin       = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10;
    HAL_GPIO_Init(GPIOE, &g);

    /* Control: PD4 = NOE(RD), PD5 = NWE(WR), PD7 = NE1(CS),
     * PD11 = A16(DC). */
    g.Pin = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_7 | GPIO_PIN_11;
    HAL_GPIO_Init(GPIOD, &g);

    /* Panel RST = PD13 (GPIO). Backlight = PA1 is owned by the
     * TIM2_CH2 PWM driver (backlight.c), not plain GPIO here. */
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Alternate = 0;
    g.Pin   = GPIO_PIN_13;
    HAL_GPIO_Init(GPIOD, &g);

    /* FSMC NOR/SRAM, bank 1 (NE1), 8-bit, vendor timing. */
    s_sram.Instance = FSMC_NORSRAM_DEVICE;
    s_sram.Extended = FSMC_NORSRAM_EXTENDED_DEVICE;
    s_sram.Init.NSBank             = FSMC_NORSRAM_BANK1;
    s_sram.Init.DataAddressMux     = FSMC_DATA_ADDRESS_MUX_DISABLE;
    s_sram.Init.MemoryType         = FSMC_MEMORY_TYPE_NOR;
    s_sram.Init.MemoryDataWidth    = FSMC_NORSRAM_MEM_BUS_WIDTH_8;
    s_sram.Init.BurstAccessMode    = FSMC_BURST_ACCESS_MODE_DISABLE;
    s_sram.Init.WaitSignalPolarity = FSMC_WAIT_SIGNAL_POLARITY_LOW;
    s_sram.Init.WaitSignalActive   = FSMC_WAIT_TIMING_BEFORE_WS;
    s_sram.Init.WriteOperation     = FSMC_WRITE_OPERATION_ENABLE;
    s_sram.Init.WaitSignal         = FSMC_WAIT_SIGNAL_DISABLE;
    s_sram.Init.ExtendedMode       = FSMC_EXTENDED_MODE_DISABLE;
    s_sram.Init.AsynchronousWait   = FSMC_ASYNCHRONOUS_WAIT_DISABLE;
    s_sram.Init.WriteBurst         = FSMC_WRITE_BURST_DISABLE;

    s_timing.AddressSetupTime      = 0x02U;
    s_timing.AddressHoldTime       = 0x00U;
    /* DATAST starts at the debug-slow value; LCD_FsmcSetTiming() ramps
     * it at runtime (main.c speed steps). */
    s_timing.DataSetupTime         = s_datast;
    s_timing.BusTurnAroundDuration = 0x00U;
    s_timing.CLKDivision           = 0x00U;
    s_timing.DataLatency           = 0x00U;
    s_timing.AccessMode            = FSMC_ACCESS_MODE_B;

    (void)HAL_SRAM_Init(&s_sram, &s_timing, &s_timing);
}

/* Runtime DATAST adjust (debug speed ramp): re-applies the NOR timing
 * with a new data-setup phase. */
void LCD_FsmcSetTiming(uint8_t datast)
{
    s_datast = datast;
    s_timing.DataSetupTime = datast;
    (void)HAL_SRAM_Init(&s_sram, &s_timing, &s_timing);
}
