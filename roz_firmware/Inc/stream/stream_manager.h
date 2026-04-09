#ifndef STREAM_MANAGER_H
#define STREAM_MANAGER_H

#include "protocol_defs.h"

#define MAX_CONCURRENT_STREAMS 2

void stream_manager_init(void);
nack_reason_t stream_open(const protocol_message_t *msg);
nack_reason_t stream_data(const protocol_message_t *msg);
nack_reason_t stream_close(const protocol_message_t *msg);
void stream_close_all(void);
uint8_t stream_active_count(void);

#endif
