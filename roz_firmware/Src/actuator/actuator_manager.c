#include "actuator_manager.h"
#include "device_registry.h"
#include <string.h>
#include <math.h>

#define MAX_ACTUATORS 8

static actuator_state_t states[MAX_ACTUATORS];
static uint32_t last_tick_ms;

/* --- Queue helpers --- */

static bool queue_push(actuator_state_t *s, const actuator_command_t *cmd)
{
    if (s->queue_count >= ACTUATOR_QUEUE_DEPTH)
        return false;
    s->queue[s->queue_head] = *cmd;
    s->queue_head = (s->queue_head + 1) % ACTUATOR_QUEUE_DEPTH;
    s->queue_count++;
    return true;
}

static bool queue_pop(actuator_state_t *s, actuator_command_t *cmd)
{
    if (s->queue_count == 0)
        return false;
    *cmd = s->queue[s->queue_tail];
    s->queue_tail = (s->queue_tail + 1) % ACTUATOR_QUEUE_DEPTH;
    s->queue_count--;
    return true;
}

static void queue_clear(actuator_state_t *s)
{
    s->queue_head = 0;
    s->queue_tail = 0;
    s->queue_count = 0;
}

/* --- Begin executing a command on an actuator --- */

static void begin_command(uint8_t idx, const actuator_command_t *cmd)
{
    actuator_state_t *s = &states[idx];
    const actuator_config_t *cfg = &ACTUATOR_TABLE[idx];

    s->target_position = cmd->target_position;
    s->speed = cmd->speed;
    s->status = ACT_STATUS_MOVING;
    cfg->driver->enable(cfg);
}

/* --- Transition to idle/hold when motion completes --- */

static void finish_motion(uint8_t idx)
{
    actuator_state_t *s = &states[idx];
    const actuator_config_t *cfg = &ACTUATOR_TABLE[idx];

    /* Try dequeuing next command */
    actuator_command_t next;
    if (queue_pop(s, &next)) {
        begin_command(idx, &next);
        return;
    }

    /* No more commands — apply hold behavior */
    if (cfg->hold == HOLD_ACTIVE) {
        s->status = ACT_STATUS_HOLDING;
        cfg->driver->enable(cfg);
    } else {
        s->status = ACT_STATUS_IDLE;
        cfg->driver->disable(cfg);
    }
}

/* --- Public API --- */

void actuator_manager_init(void)
{
    memset(states, 0, sizeof(states));
    last_tick_ms = 0;

    for (uint8_t i = 0; i < ACTUATOR_COUNT && i < MAX_ACTUATORS; i++) {
        const actuator_config_t *cfg = &ACTUATOR_TABLE[i];
        cfg->driver->init(cfg);
        states[i].status = ACT_STATUS_IDLE;
        states[i].current_position = cfg->default_position;
        states[i].target_position = cfg->default_position;
    }
}

void actuator_manager_tick(uint32_t now_ms)
{
    uint32_t dt_ms = now_ms - last_tick_ms;
    last_tick_ms = now_ms;

    if (dt_ms == 0)
        return;

    float dt_s = (float)dt_ms / 1000.0f;

    for (uint8_t i = 0; i < ACTUATOR_COUNT && i < MAX_ACTUATORS; i++) {
        actuator_state_t *s = &states[i];
        const actuator_config_t *cfg = &ACTUATOR_TABLE[i];

        if (s->status != ACT_STATUS_MOVING && s->status != ACT_STATUS_HOMING)
            continue;

        float step = s->speed * dt_s;
        float diff = s->target_position - s->current_position;

        if (fabsf(diff) <= step) {
            /* Reached target */
            s->current_position = s->target_position;
            cfg->driver->set_position(cfg, s->current_position);
            finish_motion(i);
        } else {
            /* Step toward target */
            if (diff > 0.0f)
                s->current_position += step;
            else
                s->current_position -= step;
            cfg->driver->set_position(cfg, s->current_position);
        }
    }
}

nack_reason_t actuator_command(actuator_id_t id, float position, float speed,
                               action_mode_t mode)
{
    const actuator_config_t *cfg = registry_find_actuator(id);
    if (!cfg)
        return NACK_ACTUATOR_NOT_FOUND;
    if (id >= MAX_ACTUATORS)
        return NACK_ACTUATOR_NOT_FOUND;

    actuator_state_t *s = &states[id];

    if (s->status == ACT_STATUS_ESTOP)
        return NACK_EMERGENCY_STOP;

    if (position < cfg->min_position || position > cfg->max_position)
        return NACK_POSITION_OUT_OF_RANGE;

    actuator_command_t cmd = {
        .target_position = position,
        .speed = speed,
        .action_mode = mode,
    };

    if (mode == ACTION_OVERRIDE) {
        queue_clear(s);
        begin_command(id, &cmd);
    } else {
        /* Queue mode */
        if (s->status == ACT_STATUS_IDLE || s->status == ACT_STATUS_HOLDING) {
            begin_command(id, &cmd);
        } else {
            if (!queue_push(s, &cmd))
                return NACK_DEVICE_BUSY;
        }
    }

    return NACK_UNKNOWN;  /* success (0x00) */
}

nack_reason_t actuator_coordinated_command(const actuator_command_t *cmds,
                                           const actuator_id_t *ids,
                                           uint8_t count)
{
    /* Validate all first */
    for (uint8_t i = 0; i < count; i++) {
        const actuator_config_t *cfg = registry_find_actuator(ids[i]);
        if (!cfg)
            return NACK_ACTUATOR_NOT_FOUND;
        if (ids[i] >= MAX_ACTUATORS)
            return NACK_ACTUATOR_NOT_FOUND;
        if (states[ids[i]].status == ACT_STATUS_ESTOP)
            return NACK_EMERGENCY_STOP;
        if (cmds[i].target_position < cfg->min_position ||
            cmds[i].target_position > cfg->max_position)
            return NACK_POSITION_OUT_OF_RANGE;
    }

    /* Apply atomically — coordinated commands are always override */
    for (uint8_t i = 0; i < count; i++) {
        actuator_state_t *s = &states[ids[i]];
        queue_clear(s);
        begin_command(ids[i], &cmds[i]);
    }

    return NACK_UNKNOWN;  /* success */
}

const actuator_state_t *actuator_get_state(actuator_id_t id)
{
    if (id >= ACTUATOR_COUNT || id >= MAX_ACTUATORS)
        return NULL;
    return &states[id];
}

void actuator_emergency_stop(void)
{
    for (uint8_t i = 0; i < ACTUATOR_COUNT && i < MAX_ACTUATORS; i++) {
        const actuator_config_t *cfg = &ACTUATOR_TABLE[i];
        queue_clear(&states[i]);
        cfg->driver->disable(cfg);
        states[i].status = ACT_STATUS_ESTOP;
        states[i].speed = 0.0f;
    }
}

void actuator_clear_emergency_stop(void)
{
    for (uint8_t i = 0; i < ACTUATOR_COUNT && i < MAX_ACTUATORS; i++) {
        if (states[i].status == ACT_STATUS_ESTOP) {
            states[i].status = ACT_STATUS_IDLE;
            states[i].target_position = states[i].current_position;
        }
    }
}

void actuator_homing_start(void)
{
    for (uint8_t i = 0; i < ACTUATOR_COUNT && i < MAX_ACTUATORS; i++) {
        const actuator_config_t *cfg = &ACTUATOR_TABLE[i];
        actuator_state_t *s = &states[i];

        switch (cfg->homing) {
        case HOMING_RETURN_TO_DEFAULT:
            s->target_position = cfg->default_position;
            s->current_position = cfg->default_position;
            s->speed = 0.0f;
            s->status = ACT_STATUS_HOMING;
            cfg->driver->enable(cfg);
            cfg->driver->set_position(cfg, cfg->default_position);
            /* Homing completes immediately for servos (we assume position) */
            finish_motion(i);
            break;
        case HOMING_CALIBRATION:
            s->status = ACT_STATUS_CALIBRATING;
            /* Future: implement calibration sequence */
            break;
        case HOMING_NONE:
        default:
            s->status = ACT_STATUS_IDLE;
            break;
        }
    }
}

bool actuator_homing_complete(void)
{
    for (uint8_t i = 0; i < ACTUATOR_COUNT && i < MAX_ACTUATORS; i++) {
        if (states[i].status == ACT_STATUS_HOMING ||
            states[i].status == ACT_STATUS_CALIBRATING)
            return false;
    }
    return true;
}
