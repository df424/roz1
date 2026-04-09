#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "protocol_defs.h"
#include "framing.h"

void protocol_init(framing_state_t *framing);

/* Parse header and dispatch to appropriate handler. */
void protocol_dispatch(const frame_t *frame);

/* Outgoing helpers */
void protocol_send_ack(seq_num_t ref_seq);
void protocol_send_nack(seq_num_t ref_seq, nack_reason_t reason);
seq_num_t protocol_next_seq(void);

/* Build a 9-byte wire header into buf (must be >= FRAME_HEADER_SIZE). */
void protocol_build_header(uint8_t *buf, msg_type_t type, seq_num_t seq,
                           uint8_t flags, uint16_t payload_len,
                           sync_tag_t sync_tag);

/* Parse a 9-byte wire header from raw bytes into structured form. */
void protocol_parse_header(const uint8_t *raw, frame_header_t *hdr);

/* Send a complete message (header auto-built). Returns the seq_num used. */
seq_num_t protocol_send_msg(msg_type_t type, const uint8_t *payload,
                            uint16_t payload_len);

#endif
