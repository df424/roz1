#ifndef CLOCK_SYNC_H
#define CLOCK_SYNC_H

#include "protocol_defs.h"
#include <stdint.h>

void clock_sync_init(void);
void clock_sync_tick(uint32_t now_ms);
void clock_sync_on_pulse(const protocol_message_t *msg);

#endif
