#include "pal_gpio.h"
#include "main.h"

void pal_gpio_init(void)
{
    /* MX_GPIO_Init already configured LD3 */
}

void pal_led_set(bool on)
{
    HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void pal_led_toggle(void)
{
    HAL_GPIO_TogglePin(LD3_GPIO_Port, LD3_Pin);
}
