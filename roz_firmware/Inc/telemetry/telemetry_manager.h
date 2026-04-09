#ifndef TELEMETRY_MANAGER_H
#define TELEMETRY_MANAGER_H

#include <stdint.h>

void telemetry_init(void);
void telemetry_tick(uint32_t now_ms);
void telemetry_set_interval(uint16_t interval_ms);
void telemetry_send_full_state(void);
void telemetry_send_post_result(void);

#endif
