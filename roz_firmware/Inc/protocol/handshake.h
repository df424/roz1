#ifndef HANDSHAKE_H
#define HANDSHAKE_H

#include "protocol_defs.h"

typedef enum {
    HS_IDLE,
    HS_WAIT_REMOTE,
    HS_WAIT_CONFIRM,
    HS_COMPLETE,
    HS_FAILED,
} handshake_state_t;

typedef struct {
    handshake_state_t state;
    uint8_t           selected_version;
    uint16_t          effective_frame_size;
} handshake_result_t;

void handshake_init(void);
void handshake_start(void);
void handshake_on_version_request(const protocol_message_t *msg);
void handshake_on_version_confirm(const protocol_message_t *msg);
handshake_state_t handshake_get_state(void);
const handshake_result_t *handshake_result(void);

#endif
