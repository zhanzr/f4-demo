/*
  st7735_test main for the nano-f411 (STM32F411CEU6 @ 100 MHz).
  Mirrors the vendor example C8T6_md144_t1 main loop (TEST_STAND):
  frame -> 16-level gray -> color bands -> full red/green/blue/white/black,
  with a 500 ms pause between screens. Backlight (PB9) runs at ~20% PWM.

  Wiring (same as vendor, BL moves to PB9 PWM):
    SCL PA5, SDA PA6, RES PA7, DC PA4, CS PB8, BL PB9
*/

#include <stdio.h>
#include "board.h"
#include "lcd.h"
#include "backlight.h"

static void TEST_STAND(void)
{
    DispFrame();                 /* bordered frame */
    StopDelay(Delay_Time);

    DispGrayHor16();             /* 16-level horizontal grayscale */
    StopDelay(Delay_Time);

    DispBand();                  /* color bands */
    StopDelay(Delay_Time);

    DispColor(RED);
    StopDelay(Delay_Time);

    DispColor(GREEN);
    StopDelay(Delay_Time);

    DispColor(BLUE);
    StopDelay(Delay_Time);

    DispColor(WHITE);
    StopDelay(Delay_Time);

    DispColor(BLACK);
    StopDelay(Delay_Time);
}

int main(void)
{
    HAL_Init();
    Board_Init();

    printf("\r\n==== nano-f411 (STM32F411CEU6) st7735_test @ %lu MHz ====\r\n",
           (unsigned long)(SystemCoreClock / 1000000UL));
    printf("ST7735S 1.44\" 128x128, 4-wire SPI: SCL=PA5 SDA=PA6 RES=PA7 "
           "DC=PA4 CS=PB8 BL=PB9(PWM ~20%%)\r\n");

    Backlight_Init();
    LCD_Init();

    while (1)
    {
        TEST_STAND();
    }

    return 0;
}