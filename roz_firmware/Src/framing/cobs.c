#include "cobs.h"

uint16_t cobs_encode(const uint8_t *input, uint16_t len, uint8_t *output)
{
    uint16_t read_idx = 0;
    uint16_t write_idx = 1;   /* skip code byte placeholder */
    uint16_t code_idx = 0;    /* position of current code byte */
    uint8_t code = 1;

    while (read_idx < len) {
        if (input[read_idx] == 0x00) {
            output[code_idx] = code;
            code_idx = write_idx++;
            code = 1;
            read_idx++;
        } else {
            output[write_idx++] = input[read_idx++];
            code++;
            if (code == 0xFF) {
                output[code_idx] = code;
                code_idx = write_idx++;
                code = 1;
            }
        }
    }
    output[code_idx] = code;

    return write_idx;
}

uint16_t cobs_decode(const uint8_t *input, uint16_t len, uint8_t *output)
{
    uint16_t read_idx = 0;
    uint16_t write_idx = 0;

    while (read_idx < len) {
        uint8_t code = input[read_idx++];
        if (code == 0x00)
            return 0;  /* unexpected zero in encoded data */

        for (uint8_t i = 1; i < code; i++) {
            if (read_idx >= len)
                return 0;  /* truncated */
            output[write_idx++] = input[read_idx++];
        }

        if (code < 0xFF && read_idx < len)
            output[write_idx++] = 0x00;
    }

    /* Remove trailing zero if present (the final group doesn't imply one) */
    if (write_idx > 0 && output[write_idx - 1] == 0x00)
        write_idx--;

    return write_idx;
}
