/**
  * @file    jpeg_test/src/main.c
  * @brief   JPEG-output probe for the fire-f429 OV5640 ("FD5640") module.
  *
  * Console-only test (USART1 115200). It does NOT touch the LCD.
  *
  *  - board init: 180 MHz, LED GPIO, USART1
  *  - runs the probe: ID read, colorbar sanity capture, then JPEG modes
  *  - leaves the CPU in a loop (never returns)
  */

#include "board.h"
#include "probe_jpeg.h"

#include <stdio.h>

/* The OV5640 driver's VSYNC callback increments this frame counter. */
uint8_t fps = 0;

int main(void)
{
    HAL_Init();
    Board_Init();

    printf("\r\n=== jpeg_test: probing OV5640 JPEG output ===\r\n");

    ProbeJpeg_Run();

    /* NOTREACHED - ProbeJpeg_Run ends in an infinite loop. */
    return 0;
}