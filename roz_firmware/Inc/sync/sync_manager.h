#ifndef SYNC_MANAGER_H
#define SYNC_MANAGER_H

#include "protocol_defs.h"

#define MAX_SYNC_GROUPS   2
#define SYNC_BUFFER_SIZE  128

void sync_manager_init(void);
void sync_manager_tick(uint32_t now_ms);

nack_reason_t sync_buffer_message(sync_tag_t tag, const protocol_message_t *msg);
nack_reason_t sync_execute(sync_tag_t tag);

#endif
