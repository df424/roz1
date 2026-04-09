#ifndef PAL_GPIO_H
#define PAL_GPIO_H

#include <stdbool.h>

void pal_gpio_init(void);
void pal_led_set(bool on);
void pal_led_toggle(void);

#endif
