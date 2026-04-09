#ifndef DEVICE_REGISTRY_H
#define DEVICE_REGISTRY_H

#include "actuator_types.h"

extern const actuator_config_t ACTUATOR_TABLE[];
extern const uint8_t ACTUATOR_COUNT;

extern const sensor_config_t SENSOR_TABLE[];
extern const uint8_t SENSOR_COUNT;

const actuator_config_t *registry_find_actuator(actuator_id_t id);
const sensor_config_t *registry_find_sensor(sensor_id_t id);

#endif
