#ifndef FRAMING_H
#define FRAMING_H

#include <stdint.h>
#include <stdbool.h>
#include "pal_transport.h"

#define FRAME_BUF_SIZE  256
#define COBS_BUF_SIZE   280  /* FRAME_BUF_SIZE + overhead + margin */
#define FRAME_HEADER_SIZE 9
#define FRAME_CRC_SIZE    2

typedef struct {
    uint8_t  raw[FRAME_BUF_SIZE];   /* decoded frame (header + payload + crc) */
    uint16_t len;                   /* total decoded length */
    bool     valid;                 /* CRC passed */
} frame_t;

typedef struct {
    /* RX accumulation buffer (COBS-encoded bytes before delimiter) */
    uint8_t  rx_accum[COBS_BUF_SIZE];
    uint16_t rx_accum_len;
    /* TX scratch */
    uint8_t  tx_raw[FRAME_BUF_SIZE];
    uint8_t  tx_cobs[COBS_BUF_SIZE];
    /* Transport reference */
    pal_transport_t *transport;
} framing_state_t;

void framing_init(framing_state_t *state, pal_transport_t *transport);

/* Poll transport for incoming frames. Returns true when a complete valid frame is ready. */
bool framing_poll(framing_state_t *state, frame_t *out);

/* Build and send a frame: header (FRAME_HEADER_SIZE bytes) + payload. */
bool framing_send(framing_state_t *state, const uint8_t *header,
                  const uint8_t *payload, uint16_t payload_len);

#endif
