#ifndef PAL_TRANSPORT_H
#define PAL_TRANSPORT_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    bool (*send)(const uint8_t *data, uint16_t len);
    uint16_t (*receive)(uint8_t *buf, uint16_t max_len);
    bool (*tx_busy)(void);
} pal_transport_t;

void pal_transport_init(pal_transport_t *tp);

#endif
