#ifndef COBS_H
#define COBS_H

#include <stdint.h>

/*
 * COBS encode: eliminates 0x00 from data so it can serve as frame delimiter.
 * output must be at least len + len/254 + 1 bytes.
 * Returns number of bytes written to output.
 */
uint16_t cobs_encode(const uint8_t *input, uint16_t len, uint8_t *output);

/*
 * COBS decode: reverses encoding.
 * output must be at least len bytes.
 * Returns number of decoded bytes, or 0 on error.
 */
uint16_t cobs_decode(const uint8_t *input, uint16_t len, uint8_t *output);

#endif
