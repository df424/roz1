#ifndef ACTUATOR_MANAGER_H
#define ACTUATOR_MANAGER_H

#include "actuator_types.h"
#include "protocol_defs.h"
#include <stdbool.h>

#define ACTUATOR_QUEUE_DEPTH 4

typedef enum {
    ACT_STATUS_IDLE,
    ACT_STATUS_MOVING,
    ACT_STATUS_HOLDING,
    ACT_STATUS_HOMING,
    ACT_STATUS_CALIBRATING,
    ACT_STATUS_FAULT,
    ACT_STATUS_ESTOP,
} actuator_status_t;

typedef struct {
    float           target_position;
    float           speed;
    action_mode_t   action_mode;
} actuator_command_t;

typedef struct {
    actuator_status_t   status;
    float               current_position;
    float               target_position;
    float               speed;
    uint8_t             fault_code;
    /* Circular command queue */
    actuator_command_t  queue[ACTUATOR_QUEUE_DEPTH];
    uint8_t             queue_head;
    uint8_t             queue_tail;
    uint8_t             queue_count;
} actuator_state_t;

void actuator_manager_init(void);
void actuator_manager_tick(uint32_t now_ms);

nack_reason_t actuator_command(actuator_id_t id, float position, float speed,
                               action_mode_t mode);
nack_reason_t actuator_coordinated_command(const actuator_command_t *cmds,
                                           const actuator_id_t *ids,
                                           uint8_t count);

const actuator_state_t *actuator_get_state(actuator_id_t id);

void actuator_emergency_stop(void);
void actuator_clear_emergency_stop(void);
void actuator_homing_start(void);
bool actuator_homing_complete(void);

#endif
