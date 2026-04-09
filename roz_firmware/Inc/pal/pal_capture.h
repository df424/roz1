#ifndef PAL_CAPTURE_H
#define PAL_CAPTURE_H

#include <stdint.h>
#include <stdbool.h>

void pal_capture_init(void);
bool pal_capture_ready(void);
uint32_t pal_capture_read_us(uint8_t *seq_out);

#endif
