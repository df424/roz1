#include "sync_manager.h"
#include "protocol.h"
#include "framing.h"
#include "pal_tick.h"
#include <string.h>

#define SYNC_TIMEOUT_MS 5000

/* Each buffered message: header(9) + payload stored contiguously */
typedef struct {
    sync_tag_t tag;
    uint8_t    buffer[SYNC_BUFFER_SIZE];
    uint16_t   used;
    uint8_t    msg_count;
    uint32_t   timestamp_ms;
    bool       active;
} sync_group_t;

static sync_group_t groups[MAX_SYNC_GROUPS];

void sync_manager_init(void)
{
    memset(groups, 0, sizeof(groups));
}

void sync_manager_tick(uint32_t now_ms)
{
    for (uint8_t i = 0; i < MAX_SYNC_GROUPS; i++) {
        if (groups[i].active &&
            (now_ms - groups[i].timestamp_ms) > SYNC_TIMEOUT_MS) {
            groups[i].active = false;
            groups[i].used = 0;
            groups[i].msg_count = 0;
        }
    }
}

nack_reason_t sync_buffer_message(sync_tag_t tag, const protocol_message_t *msg)
{
    /* Find existing group with this tag, or allocate a new one */
    sync_group_t *grp = NULL;

    for (uint8_t i = 0; i < MAX_SYNC_GROUPS; i++) {
        if (groups[i].active && groups[i].tag == tag) {
            grp = &groups[i];
            break;
        }
    }

    if (!grp) {
        for (uint8_t i = 0; i < MAX_SYNC_GROUPS; i++) {
            if (!groups[i].active) {
                grp = &groups[i];
                grp->tag = tag;
                grp->used = 0;
                grp->msg_count = 0;
                grp->active = true;
                break;
            }
        }
    }

    if (!grp)
        return NACK_DEVICE_BUSY;

    /*
     * Store: 2 bytes msg_type + 2 bytes payload_len + payload.
     * This is enough to replay through dispatch.
     */
    uint16_t entry_size = 4 + msg->payload_len;
    if (grp->used + entry_size > SYNC_BUFFER_SIZE)
        return NACK_DEVICE_BUSY;

    uint8_t *p = &grp->buffer[grp->used];
    p[0] = (uint8_t)(msg->header->msg_type & 0xFF);
    p[1] = (uint8_t)(msg->header->msg_type >> 8);
    p[2] = (uint8_t)(msg->payload_len & 0xFF);
    p[3] = (uint8_t)(msg->payload_len >> 8);
    if (msg->payload_len > 0)
        memcpy(&p[4], msg->payload, msg->payload_len);

    grp->used += entry_size;
    grp->msg_count++;
    grp->timestamp_ms = pal_tick_ms();

    return NACK_UNKNOWN;
}

nack_reason_t sync_execute(sync_tag_t tag)
{
    sync_group_t *grp = NULL;
    for (uint8_t i = 0; i < MAX_SYNC_GROUPS; i++) {
        if (groups[i].active && groups[i].tag == tag) {
            grp = &groups[i];
            break;
        }
    }

    if (!grp)
        return NACK_UNKNOWN;  /* no-op per spec */

    /* Replay each buffered message through dispatch with sync_tag=0 */
    uint16_t offset = 0;
    while (offset < grp->used) {
        uint8_t *p = &grp->buffer[offset];
        msg_type_t type = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
        uint16_t plen   = (uint16_t)p[2] | ((uint16_t)p[3] << 8);

        /* Build a temporary frame header with sync_tag=0 */
        frame_header_t hdr = {
            .msg_type = type,
            .seq_num = 0,
            .flags = TRANSFER_COMPLETE,
            .payload_length = plen,
            .sync_tag = 0,
        };
        protocol_message_t msg = {
            .header = &hdr,
            .payload = &p[4],
            .payload_len = plen,
        };

        /*
         * Build a synthetic frame_t and re-dispatch.
         * We construct the raw bytes so protocol_dispatch can parse them.
         */
        frame_t synth;
        protocol_build_header(synth.raw, type, 0, TRANSFER_COMPLETE, plen, 0);
        if (plen > 0 && plen <= FRAME_BUF_SIZE - FRAME_HEADER_SIZE)
            memcpy(synth.raw + FRAME_HEADER_SIZE, &p[4], plen);
        synth.len = FRAME_HEADER_SIZE + plen;
        synth.valid = true;
        protocol_dispatch(&synth);

        offset += 4 + plen;
    }

    grp->active = false;
    grp->used = 0;
    grp->msg_count = 0;

    return NACK_UNKNOWN;
}
