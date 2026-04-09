#include "telemetry_manager.h"
#include "protocol.h"
#include "actuator_manager.h"
#include "device_registry.h"
#include "stream_manager.h"
#include "system_manager.h"
#include "pal_tick.h"
#include <string.h>

static uint16_t push_interval_ms;
static uint32_t last_push_ms;

/* POST results storage */
#define MAX_POST_RESULTS 8
static struct {
    uint8_t device_id;
    uint8_t device_type;  /* 0x01=actuator, 0x02=sensor */
    uint8_t result;       /* 0x00=pass, 0x01=fail, 0x02=skipped */
} post_results[MAX_POST_RESULTS];
static uint8_t post_result_count;

void telemetry_init(void)
{
    push_interval_ms = 0;
    last_push_ms = 0;
    post_result_count = 0;

    /* Record POST results for all actuators (assume pass for now) */
    for (uint8_t i = 0; i < ACTUATOR_COUNT && i < MAX_POST_RESULTS; i++) {
        post_results[i].device_id = ACTUATOR_TABLE[i].id;
        post_results[i].device_type = 0x01;
        post_results[i].result = 0x00;  /* pass */
        post_result_count++;
    }
}

void telemetry_set_interval(uint16_t interval_ms)
{
    push_interval_ms = interval_ms;
}

static void send_actuator_telemetry(void)
{
    /* controller_time_us(4) + actuator_count(1) + entries(12*N) */
    uint8_t payload[5 + 12 * 4];  /* max 4 actuators */
    uint16_t offset = 0;

    uint32_t now_us = pal_tick_us();
    payload[offset++] = (uint8_t)(now_us & 0xFF);
    payload[offset++] = (uint8_t)((now_us >> 8) & 0xFF);
    payload[offset++] = (uint8_t)((now_us >> 16) & 0xFF);
    payload[offset++] = (uint8_t)((now_us >> 24) & 0xFF);

    uint8_t count = ACTUATOR_COUNT;
    if (count > 4) count = 4;
    payload[offset++] = count;

    for (uint8_t i = 0; i < count; i++) {
        const actuator_state_t *s = actuator_get_state(i);
        if (!s) continue;

        payload[offset++] = ACTUATOR_TABLE[i].id;
        payload[offset++] = (uint8_t)s->status;

        float cp = s->current_position;
        float tp = s->target_position;
        memcpy(&payload[offset], &cp, 4); offset += 4;
        memcpy(&payload[offset], &tp, 4); offset += 4;

        payload[offset++] = s->queue_count;
        payload[offset++] = s->fault_code;
    }

    protocol_send_msg(MSG_ACTUATOR_TELEMETRY, payload, offset);
}

static void send_sensor_telemetry(void)
{
    uint8_t payload[5];
    uint32_t now_us = pal_tick_us();
    payload[0] = (uint8_t)(now_us & 0xFF);
    payload[1] = (uint8_t)((now_us >> 8) & 0xFF);
    payload[2] = (uint8_t)((now_us >> 16) & 0xFF);
    payload[3] = (uint8_t)((now_us >> 24) & 0xFF);
    payload[4] = 0;  /* no sensors on reference hardware */

    protocol_send_msg(MSG_SENSOR_TELEMETRY, payload, sizeof(payload));
}

static void send_system_telemetry(void)
{
    uint8_t payload[6];
    uint32_t uptime_s = pal_tick_ms() / 1000;

    payload[0] = (uint8_t)system_get_state();
    payload[1] = (uint8_t)(uptime_s & 0xFF);
    payload[2] = (uint8_t)((uptime_s >> 8) & 0xFF);
    payload[3] = stream_active_count();
    payload[4] = 0;  /* cpu_load: not measured yet */
    payload[5] = 0;  /* led_state */

    protocol_send_msg(MSG_SYSTEM_TELEMETRY, payload, sizeof(payload));
}

void telemetry_tick(uint32_t now_ms)
{
    if (push_interval_ms == 0)
        return;

    if ((now_ms - last_push_ms) >= push_interval_ms) {
        last_push_ms = now_ms;
        send_actuator_telemetry();
        send_sensor_telemetry();
        send_system_telemetry();
    }
}

void telemetry_send_full_state(void)
{
    send_actuator_telemetry();
    send_sensor_telemetry();
    send_system_telemetry();
}

void telemetry_send_post_result(void)
{
    uint8_t payload[1 + 3 * MAX_POST_RESULTS];
    payload[0] = post_result_count;
    for (uint8_t i = 0; i < post_result_count; i++) {
        payload[1 + i * 3 + 0] = post_results[i].device_id;
        payload[1 + i * 3 + 1] = post_results[i].device_type;
        payload[1 + i * 3 + 2] = post_results[i].result;
    }
    protocol_send_msg(MSG_POST_RESULT, payload,
                      1 + post_result_count * 3);
}
