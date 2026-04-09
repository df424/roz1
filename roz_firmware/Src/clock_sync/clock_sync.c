#include "clock_sync.h"
#include "protocol.h"
#include "pal_capture.h"
#include "pal_tick.h"
#include <string.h>

#define CAPTURE_TIMEOUT_MS 5000

static uint32_t capture_us;
static uint8_t  capture_seq;
static bool     capture_pending;
static uint32_t capture_time_ms;

void clock_sync_init(void)
{
    capture_pending = false;
    capture_us = 0;
    capture_seq = 0;
}

void clock_sync_tick(uint32_t now_ms)
{
    /* Check for new hardware capture */
    if (pal_capture_ready()) {
        uint8_t seq;
        capture_us = pal_capture_read_us(&seq);
        capture_seq = seq;
        capture_pending = true;
        capture_time_ms = now_ms;
    }

    /* Discard stale captures */
    if (capture_pending && (now_ms - capture_time_ms) > CAPTURE_TIMEOUT_MS)
        capture_pending = false;
}

void clock_sync_on_pulse(const protocol_message_t *msg)
{
    if (msg->payload_len < 5) {
        protocol_send_nack(msg->header->seq_num, NACK_INVALID_PAYLOAD);
        return;
    }

    /* Parse pulse_seq from the message */
    uint8_t pulse_seq = msg->payload[4];

    protocol_send_ack(msg->header->seq_num);

    if (!capture_pending || capture_seq != pulse_seq) {
        protocol_send_nack(msg->header->seq_num, NACK_INVALID_PAYLOAD);
        return;
    }

    /* Send TimeSyncReport with our captured timestamp */
    uint8_t payload[5];
    payload[0] = (uint8_t)(capture_us & 0xFF);
    payload[1] = (uint8_t)((capture_us >> 8) & 0xFF);
    payload[2] = (uint8_t)((capture_us >> 16) & 0xFF);
    payload[3] = (uint8_t)((capture_us >> 24) & 0xFF);
    payload[4] = pulse_seq;

    protocol_send_msg(MSG_TIMESYNC_REPORT, payload, sizeof(payload));

    capture_pending = false;
}
