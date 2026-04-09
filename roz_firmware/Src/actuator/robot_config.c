#include "device_registry.h"
#include "servo_driver.h"
#include <stddef.h>

/* --- Servo driver configs (one per physical servo) --- */

static const servo_config_t servo_neck  = {
    .pwm_channel = 0, .min_pulse_us = 1000, .max_pulse_us = 2000
};
static const servo_config_t servo_eye_h = {
    .pwm_channel = 1, .min_pulse_us = 1000, .max_pulse_us = 2000
};
static const servo_config_t servo_eye_v = {
    .pwm_channel = 2, .min_pulse_us = 1000, .max_pulse_us = 2000
};
static const servo_config_t servo_jaw   = {
    .pwm_channel = 3, .min_pulse_us = 1000, .max_pulse_us = 2000
};

/* --- Actuator table --- */

const actuator_config_t ACTUATOR_TABLE[] = {
    { /* neck yaw */
        .id = 0, .type = ACTUATOR_SERVO,
        .homing = HOMING_RETURN_TO_DEFAULT, .hold = HOLD_ACTIVE,
        .min_position = 0.0f, .max_position = 1.0f,
        .default_position = 0.5f,
        .driver = &servo_driver, .driver_config = &servo_neck,
    },
    { /* eye horizontal */
        .id = 1, .type = ACTUATOR_SERVO,
        .homing = HOMING_RETURN_TO_DEFAULT, .hold = HOLD_ACTIVE,
        .min_position = 0.0f, .max_position = 1.0f,
        .default_position = 0.5f,
        .driver = &servo_driver, .driver_config = &servo_eye_h,
    },
    { /* eye vertical */
        .id = 2, .type = ACTUATOR_SERVO,
        .homing = HOMING_RETURN_TO_DEFAULT, .hold = HOLD_ACTIVE,
        .min_position = 0.0f, .max_position = 1.0f,
        .default_position = 0.5f,
        .driver = &servo_driver, .driver_config = &servo_eye_v,
    },
    { /* jaw */
        .id = 3, .type = ACTUATOR_SERVO,
        .homing = HOMING_RETURN_TO_DEFAULT, .hold = HOLD_PASSIVE,
        .min_position = 0.0f, .max_position = 1.0f,
        .default_position = 0.0f,
        .driver = &servo_driver, .driver_config = &servo_jaw,
    },
};
const uint8_t ACTUATOR_COUNT = sizeof(ACTUATOR_TABLE) / sizeof(ACTUATOR_TABLE[0]);

/* --- Sensor table (none on reference hardware) --- */

const sensor_config_t SENSOR_TABLE[] = { {0} };
const uint8_t SENSOR_COUNT = 0;

/* --- Registry lookup --- */

const actuator_config_t *registry_find_actuator(actuator_id_t id)
{
    for (uint8_t i = 0; i < ACTUATOR_COUNT; i++) {
        if (ACTUATOR_TABLE[i].id == id)
            return &ACTUATOR_TABLE[i];
    }
    return NULL;
}

const sensor_config_t *registry_find_sensor(sensor_id_t id)
{
    for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
        if (SENSOR_TABLE[i].id == id)
            return &SENSOR_TABLE[i];
    }
    return NULL;
}
