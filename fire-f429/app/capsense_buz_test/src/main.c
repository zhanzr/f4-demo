/*
 * Capacitive touch pad on PA5 (TIM2_CH1 input capture) + active buzzer PI11.
 *
 * Behavior:
 *   - Press the capsense pad  -> buzzer ON.
 *   - Release the pad         -> buzzer OFF, but the buzzer stays on for at
 *                                least 500 ms from the press (minimum
 *                                period), even for a very short tap.
 *
 * Buzzer: PI11 drives the base of an NPN BJT; HIGH = active buzzer ON.
 * The capsense driver is in capsense.c (TIM2_CH1 input capture, ported from
 * the Wildfire F429 "TIM-电容按键" example).
 */
#include <stdio.h>
#include "board.h"
#include "capsense.h"

/* --- Active buzzer on PI11 (NPN BJT base, HIGH = ON) ---------------------- */
#define BUZZER_GPIO_PORT   GPIOI
#define BUZZER_PIN         GPIO_PIN_11
#define BUZZER_ON()        HAL_GPIO_WritePin(BUZZER_GPIO_PORT, BUZZER_PIN, GPIO_PIN_SET)
#define BUZZER_OFF()       HAL_GPIO_WritePin(BUZZER_GPIO_PORT, BUZZER_PIN, GPIO_PIN_RESET)

#define BUZZER_MIN_PERIOD_MS  500U   /* minimum buzzer-on duration */

static void Buzzer_Init(void)
{
    GPIO_InitTypeDef gpio;

    __HAL_RCC_GPIOI_CLK_ENABLE();
    gpio.Pin   = BUZZER_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_LOW;
    gpio.Pull  = GPIO_NOPULL;
    HAL_GPIO_Init(BUZZER_GPIO_PORT, &gpio);
    BUZZER_OFF();
}

int main(void)
{
    HAL_Init();
    Board_Init();          /* 180 MHz, USART1 (PA9/PA10), LEDs */

    Buzzer_Init();
    if (CapSense_Init() != 0)
    {
        printf("capsense_buz_test: aborting (no capacitive pad?)\r\n");
        while (1) { }
    }

    printf("\r\n==== fire-f429 capsense + buzzer test ====\r\n");
    printf("Press the capsense pad (PA5) to sound the buzzer (PI11)\r\n");
    printf("Buzzer minimum ON period: %u ms\r\n", (unsigned)BUZZER_MIN_PERIOD_MS);

    int      prev_pressed = 0;
    uint32_t buzzer_on_since = 0;
    uint32_t last_raw_print = 0;

    while (1)
    {
        int pressed = CapSense_Scan();

        if (HAL_GetTick() - last_raw_print >= 1000)
        {
            last_raw_print = HAL_GetTick();
            printf("raw=%u state=%d\r\n", (unsigned)CapSense_GetRaw(), pressed);
        }

        if (pressed && !prev_pressed)
        {
            /* fresh press: drive the buzzer, start the 500 ms minimum. */
            buzzer_on_since = HAL_GetTick();
            BUZZER_ON();
            printf("PRESSED  raw=%u\r\n", (unsigned)CapSense_GetRaw());
        }
        else if (!pressed && prev_pressed)
        {
            /* released: keep the buzzer until the minimum period elapsed. */
            printf("RELEASED raw=%u\r\n", (unsigned)CapSense_GetRaw());
        }

        if (!pressed && (HAL_GetTick() - buzzer_on_since >= BUZZER_MIN_PERIOD_MS))
        {
            BUZZER_OFF();
        }
        else if (pressed)
        {
            BUZZER_ON();          /* stay on while pressed */
        }

        prev_pressed = pressed;
        HAL_Delay(20);
    }
}
