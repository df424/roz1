#include "framing.h"
#include "cobs.h"
#include "crc16.h"
#include <string.h>

void framing_init(framing_state_t *state, pal_transport_t *transport)
{
    memset(state, 0, sizeof(*state));
    state->transport = transport;
}

bool framing_poll(framing_state_t *state, frame_t *out)
{
    uint8_t buf[32];
    uint16_t n = state->transport->receive(buf, sizeof(buf));

    for (uint16_t i = 0; i < n; i++) {
        uint8_t byte = buf[i];

        if (byte == 0x00) {
            /* Frame delimiter — decode what we've accumulated */
            if (state->rx_accum_len > 0) {
                out->len = cobs_decode(state->rx_accum, state->rx_accum_len,
                                       out->raw);
                state->rx_accum_len = 0;

                if (out->len < FRAME_HEADER_SIZE + FRAME_CRC_SIZE) {
                    out->valid = false;
                    continue;
                }

                /* Verify CRC over header + payload (everything except last 2 bytes) */
                uint16_t data_len = out->len - FRAME_CRC_SIZE;
                uint16_t recv_crc = (uint16_t)out->raw[data_len]
                                  | ((uint16_t)out->raw[data_len + 1] << 8);
                uint16_t calc_crc = crc16_ccitt(out->raw, data_len);

                if (recv_crc == calc_crc) {
                    out->valid = true;
                    out->len = data_len;  /* trim CRC from length */
                    return true;
                }
                out->valid = false;
            }
        } else {
            if (state->rx_accum_len < COBS_BUF_SIZE) {
                state->rx_accum[state->rx_accum_len++] = byte;
            } else {
                /* Overflow — discard frame */
                state->rx_accum_len = 0;
            }
        }
    }

    return false;
}

bool framing_send(framing_state_t *state, const uint8_t *header,
                  const uint8_t *payload, uint16_t payload_len)
{
    uint16_t raw_len = FRAME_HEADER_SIZE + payload_len;
    if (raw_len + FRAME_CRC_SIZE > FRAME_BUF_SIZE)
        return false;

    /* Assemble raw frame: header + payload */
    memcpy(state->tx_raw, header, FRAME_HEADER_SIZE);
    if (payload_len > 0)
        memcpy(state->tx_raw + FRAME_HEADER_SIZE, payload, payload_len);

    /* Append CRC (little-endian) */
    uint16_t crc = crc16_ccitt(state->tx_raw, raw_len);
    state->tx_raw[raw_len] = (uint8_t)(crc & 0xFF);
    state->tx_raw[raw_len + 1] = (uint8_t)(crc >> 8);
    raw_len += FRAME_CRC_SIZE;

    /* COBS encode */
    uint16_t cobs_len = cobs_encode(state->tx_raw, raw_len, state->tx_cobs);

    /* Send encoded data + 0x00 delimiter */
    if (!state->transport->send(state->tx_cobs, cobs_len))
        return false;

    uint8_t delim = 0x00;
    return state->transport->send(&delim, 1);
}
