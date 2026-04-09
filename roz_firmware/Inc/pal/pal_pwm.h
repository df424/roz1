#ifndef PAL_PWM_H
#define PAL_PWM_H

#include <stdint.h>

typedef uint8_t pwm_channel_id_t;

void pal_pwm_init(void);
void pal_pwm_start(pwm_channel_id_t ch);
void pal_pwm_stop(pwm_channel_id_t ch);
void pal_pwm_set_pulse(pwm_channel_id_t ch, uint16_t pulse_us);

#endif
