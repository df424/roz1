#ifndef ACTUATOR_TYPES_H
#define ACTUATOR_TYPES_H

#include <stdint.h>
#include "protocol_defs.h"
#include "pal_pwm.h"

typedef enum {
    ACTUATOR_SERVO,
    ACTUATOR_LINEAR,
    ACTUATOR_CONTINUOUS,
    ACTUATOR_BINARY,
} actuator_type_t;

typedef enum {
    HOMING_NONE,
    HOMING_RETURN_TO_DEFAULT,
    HOMING_CALIBRATION,
} homing_behavior_t;

typedef enum {
    HOLD_ACTIVE,
    HOLD_PASSIVE,
} hold_behavior_t;

/* Forward declaration */
typedef struct actuator_driver actuator_driver_t;
typedef struct actuator_config actuator_config_t;

struct actuator_driver {
    void (*init)(const actuator_config_t *cfg);
    void (*set_position)(const actuator_config_t *cfg, float position);
    void (*enable)(const actuator_config_t *cfg);
    void (*disable)(const actuator_config_t *cfg);
};

struct actuator_config {
    actuator_id_t           id;
    actuator_type_t         type;
    homing_behavior_t       homing;
    hold_behavior_t         hold;
    float                   min_position;
    float                   max_position;
    float                   default_position;
    const actuator_driver_t *driver;
    const void              *driver_config;
};

/* Sensor types */
typedef enum {
    SENSOR_AUDIO_INPUT,
    SENSOR_VIDEO_INPUT,
    SENSOR_POSITION_FEEDBACK,
    SENSOR_TEMPERATURE,
} sensor_type_t;

typedef struct {
    sensor_id_t     id;
    sensor_type_t   type;
    uint16_t        sample_rate_hz;
} sensor_config_t;

#endif
