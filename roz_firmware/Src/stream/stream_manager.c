#include "stream_manager.h"
#include <stdbool.h>
#include <string.h>

typedef struct {
    stream_id_t id;
    uint8_t     stream_type;
    device_id_t device_id;
    uint16_t    sample_rate;
    uint8_t     sample_format;
    bool        active;
} stream_state_t;

static stream_state_t streams[MAX_CONCURRENT_STREAMS];

void stream_manager_init(void)
{
    memset(streams, 0, sizeof(streams));
}

nack_reason_t stream_open(const protocol_message_t *msg)
{
    if (msg->payload_len < 8)
        return NACK_INVALID_PAYLOAD;

    stream_id_t id      = msg->payload[0];
    uint8_t type        = msg->payload[1];
    device_id_t dev     = msg->payload[2];
    uint16_t rate       = (uint16_t)msg->payload[3] | ((uint16_t)msg->payload[4] << 8);
    uint8_t fmt         = msg->payload[5];

    /* Check for duplicate stream_id */
    for (uint8_t i = 0; i < MAX_CONCURRENT_STREAMS; i++) {
        if (streams[i].active && streams[i].id == id)
            return NACK_STREAM_IN_USE;
    }

    /* Find a free slot */
    for (uint8_t i = 0; i < MAX_CONCURRENT_STREAMS; i++) {
        if (!streams[i].active) {
            streams[i].id = id;
            streams[i].stream_type = type;
            streams[i].device_id = dev;
            streams[i].sample_rate = rate;
            streams[i].sample_format = fmt;
            streams[i].active = true;
            return NACK_UNKNOWN;  /* success */
        }
    }

    return NACK_MAX_STREAMS;
}

nack_reason_t stream_data(const protocol_message_t *msg)
{
    if (msg->payload_len < 2)
        return NACK_INVALID_PAYLOAD;

    stream_id_t id = msg->payload[0];

    for (uint8_t i = 0; i < MAX_CONCURRENT_STREAMS; i++) {
        if (streams[i].active && streams[i].id == id) {
            /*
             * Future: forward data to the appropriate PAL output
             * (e.g., audio DAC buffer). For now, accept and discard.
             */
            return NACK_UNKNOWN;
        }
    }

    return NACK_STREAM_NOT_FOUND;
}

nack_reason_t stream_close(const protocol_message_t *msg)
{
    if (msg->payload_len < 1)
        return NACK_INVALID_PAYLOAD;

    stream_id_t id = msg->payload[0];

    for (uint8_t i = 0; i < MAX_CONCURRENT_STREAMS; i++) {
        if (streams[i].active && streams[i].id == id) {
            streams[i].active = false;
            return NACK_UNKNOWN;
        }
    }

    return NACK_STREAM_NOT_FOUND;
}

void stream_close_all(void)
{
    for (uint8_t i = 0; i < MAX_CONCURRENT_STREAMS; i++)
        streams[i].active = false;
}

uint8_t stream_active_count(void)
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_CONCURRENT_STREAMS; i++) {
        if (streams[i].active)
            count++;
    }
    return count;
}
