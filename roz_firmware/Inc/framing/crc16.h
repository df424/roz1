#ifndef CRC16_H
#define CRC16_H

#include <stdint.h>

/* CRC-16/CCITT: polynomial 0x1021, initial value 0xFFFF */
uint16_t crc16_ccitt(const uint8_t *data, uint16_t len);

#endif
