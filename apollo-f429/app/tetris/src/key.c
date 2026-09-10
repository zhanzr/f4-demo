/* key.c - 4 mechanical push buttons for the tetris game.
 * These IO have NO external pull resistors, so the driver enables the MCU
 * internal pulls (pull-down for PA0, pull-up for the other three):
 *
 *   KEY_UP  PA0  active HIGH  -> internal pull-DOWN   (rotate)
 *   KEY_2   PC13 active LOW   -> internal pull-UP     (left)
 *   KEY_1   PH2  active LOW   -> internal pull-UP     (drop)
 *   KEY_0   PH3  active LOW   -> internal pull-UP     (right)
 *
 * The reads are raw levels; debouncing / auto-repeat lives in the game
 * (per-button state machine, like the source project the game was ported
 * from). KEY_Read(id) returns 1 while the key is pressed.
 */
#include "key.h"
#include "stm32f4xx_hal.h"

#define KEY0      HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_3)   /* PH3  right */
#define KEY1      HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_2)   /* PH2  drop  */
#define KEY2      HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13)  /* PC13 left  */
#define WK_UP     HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0)   /* PA0  rotate*/

void KEY_Init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    /* WK_UP (PA0): no external resistor -> internal pull-down, pressed=high */
    g.Pin   = GPIO_PIN_0;
    g.Mode  = GPIO_MODE_INPUT;
    g.Pull  = GPIO_PULLDOWN;
    g.Speed = GPIO_SPEED_HIGH;
    HAL_GPIO_Init(GPIOA, &g);

    /* KEY_2 (PC13): internal pull-up, pressed=low */
    g.Pin   = GPIO_PIN_13;
    g.Pull  = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOC, &g);

    /* KEY_0 (PH3) + KEY_1 (PH2): internal pull-up, pressed=low */
    g.Pin   = GPIO_PIN_2 | GPIO_PIN_3;
    HAL_GPIO_Init(GPIOH, &g);
}

int KEY_Read(int id)
{
    switch (id)
    {
        case KEY_ID_LEFT:  return KEY2 == 0;   /* active low  */
        case KEY_ID_RIGHT: return KEY0 == 0;   /* active low  */
        case KEY_ID_ROT:   return WK_UP == 1;  /* active high */
        case KEY_ID_DROP:  return KEY1 == 0;   /* active low  */
        default:           return 0;
    }
}