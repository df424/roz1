#ifndef SERVO_DRIVER_H
#define SERVO_DRIVER_H

#include "actuator_types.h"

typedef struct {
    pwm_channel_id_t pwm_channel;
    uint16_t         min_pulse_us;
    uint16_t         max_pulse_us;
} servo_config_t;

extern const actuator_driver_t servo_driver;

#endif
