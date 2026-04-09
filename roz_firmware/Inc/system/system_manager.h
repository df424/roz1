#ifndef SYSTEM_MANAGER_H
#define SYSTEM_MANAGER_H

#include <stdint.h>

typedef enum {
    SYS_BOOT,
    SYS_POST,
    SYS_HANDSHAKE,
    SYS_RUNNING,
    SYS_ESTOP,
    SYS_DISCONNECTED,
} system_state_t;

void system_init(void);
void system_tick(uint32_t now_ms);

system_state_t system_get_state(void);

void system_on_frame_received(void);
void system_on_emergency_stop(void);
void system_on_clear_emergency_stop(void);
void system_set_state(system_state_t new_state);

#endif
