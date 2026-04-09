#include "protocol.h"
#include "handshake.h"
#include "actuator_manager.h"
#include "stream_manager.h"
#include "sync_manager.h"
#include "clock_sync.h"
#include "telemetry_manager.h"
#include "system_manager.h"
#include <string.h>

static framing_state_t *g_framing;
static seq_num_t g_seq;

void protocol_init(framing_state_t *framing)
{
    g_framing = framing;
    g_seq = 0;
}

seq_num_t protocol_next_seq(void)
{
    return g_seq++;
}

void protocol_parse_header(const uint8_t *raw, frame_header_t *hdr)
{
    hdr->msg_type       = (uint16_t)raw[0] | ((uint16_t)raw[1] << 8);
    hdr->seq_num        = (uint16_t)raw[2] | ((uint16_t)raw[3] << 8);
    hdr->flags          = raw[4];
    hdr->payload_length = (uint16_t)raw[5] | ((uint16_t)raw[6] << 8);
    hdr->sync_tag       = (uint16_t)raw[7] | ((uint16_t)raw[8] << 8);
}

void protocol_build_header(uint8_t *buf, msg_type_t type, seq_num_t seq,
                           uint8_t flags, uint16_t payload_len,
                           sync_tag_t sync_tag)
{
    buf[0] = (uint8_t)(type & 0xFF);
    buf[1] = (uint8_t)(type >> 8);
    buf[2] = (uint8_t)(seq & 0xFF);
    buf[3] = (uint8_t)(seq >> 8);
    buf[4] = flags;
    buf[5] = (uint8_t)(payload_len & 0xFF);
    buf[6] = (uint8_t)(payload_len >> 8);
    buf[7] = (uint8_t)(sync_tag & 0xFF);
    buf[8] = (uint8_t)(sync_tag >> 8);
}

void protocol_send_ack(seq_num_t ref_seq)
{
    uint8_t header[FRAME_HEADER_SIZE];
    uint8_t payload[2];
    payload[0] = (uint8_t)(ref_seq & 0xFF);
    payload[1] = (uint8_t)(ref_seq >> 8);
    protocol_build_header(header, MSG_ACK, protocol_next_seq(),
                          TRANSFER_COMPLETE, sizeof(payload), 0);
    framing_send(g_framing, header, payload, sizeof(payload));
}

void protocol_send_nack(seq_num_t ref_seq, nack_reason_t reason)
{
    uint8_t header[FRAME_HEADER_SIZE];
    uint8_t payload[3];
    payload[0] = (uint8_t)(ref_seq & 0xFF);
    payload[1] = (uint8_t)(ref_seq >> 8);
    payload[2] = (uint8_t)reason;
    protocol_build_header(header, MSG_NACK, protocol_next_seq(),
                          TRANSFER_COMPLETE, sizeof(payload), 0);
    framing_send(g_framing, header, payload, sizeof(payload));
}

seq_num_t protocol_send_msg(msg_type_t type, const uint8_t *payload,
                            uint16_t payload_len)
{
    uint8_t header[FRAME_HEADER_SIZE];
    seq_num_t seq = protocol_next_seq();
    protocol_build_header(header, type, seq, TRANSFER_COMPLETE,
                          payload_len, 0);
    framing_send(g_framing, header, payload, payload_len);
    return seq;
}

/* ---- Dispatch by message type ---- */

static void handle_actuator_cmd(const protocol_message_t *msg)
{
    if (msg->payload_len < 10) {
        protocol_send_nack(msg->header->seq_num, NACK_INVALID_PAYLOAD);
        return;
    }
    actuator_id_t id = msg->payload[0];
    action_mode_t mode = (action_mode_t)msg->payload[1];
    float target_position, speed;
    memcpy(&target_position, &msg->payload[2], 4);
    memcpy(&speed, &msg->payload[6], 4);

    nack_reason_t r = actuator_command(id, target_position, speed, mode);
    if (r == NACK_UNKNOWN)
        protocol_send_ack(msg->header->seq_num);
    else
        protocol_send_nack(msg->header->seq_num, r);
}

static void handle_coordinated_cmd(const protocol_message_t *msg)
{
    if (msg->payload_len < 1) {
        protocol_send_nack(msg->header->seq_num, NACK_INVALID_PAYLOAD);
        return;
    }
    uint8_t count = msg->payload[0];
    if (msg->payload_len < 1 + count * 10u) {
        protocol_send_nack(msg->header->seq_num, NACK_INVALID_PAYLOAD);
        return;
    }

    actuator_command_t cmds[8];
    actuator_id_t ids[8];
    if (count > 8) {
        protocol_send_nack(msg->header->seq_num, NACK_DEVICE_BUSY);
        return;
    }

    const uint8_t *p = &msg->payload[1];
    for (uint8_t i = 0; i < count; i++) {
        ids[i] = p[0];
        cmds[i].action_mode = (action_mode_t)p[1];
        memcpy(&cmds[i].target_position, &p[2], 4);
        memcpy(&cmds[i].speed, &p[6], 4);
        p += 10;
    }

    nack_reason_t r = actuator_coordinated_command(cmds, ids, count);
    if (r == NACK_UNKNOWN)
        protocol_send_ack(msg->header->seq_num);
    else
        protocol_send_nack(msg->header->seq_num, r);
}

void protocol_dispatch(const frame_t *frame)
{
    if (frame->len < FRAME_HEADER_SIZE)
        return;

    frame_header_t hdr;
    protocol_parse_header(frame->raw, &hdr);

    const uint8_t *payload = frame->raw + FRAME_HEADER_SIZE;
    uint16_t payload_len = frame->len - FRAME_HEADER_SIZE;

    protocol_message_t msg = {
        .header = &hdr,
        .payload = payload,
        .payload_len = payload_len,
    };

    /* If sync_tag is non-zero and this isn't a SyncExecute, buffer it */
    if (hdr.sync_tag != 0 && hdr.msg_type != MSG_SYNC_EXECUTE) {
        nack_reason_t r = sync_buffer_message(hdr.sync_tag, &msg);
        if (r == NACK_UNKNOWN)
            protocol_send_ack(hdr.seq_num);
        else
            protocol_send_nack(hdr.seq_num, r);
        return;
    }

    switch (hdr.msg_type) {
    /* Protocol control */
    case MSG_VERSION_REQUEST:
        handshake_on_version_request(&msg);
        break;
    case MSG_VERSION_CONFIRM:
        handshake_on_version_confirm(&msg);
        break;
    case MSG_ACK:
    case MSG_NACK:
        /* Informational — no action needed on controller side for now */
        break;
    case MSG_SYNC_EXECUTE:
        if (payload_len >= 2) {
            sync_tag_t tag = (uint16_t)payload[0] | ((uint16_t)payload[1] << 8);
            nack_reason_t r = sync_execute(tag);
            if (r == NACK_UNKNOWN)
                protocol_send_ack(hdr.seq_num);
            else
                protocol_send_nack(hdr.seq_num, r);
        } else {
            protocol_send_nack(hdr.seq_num, NACK_INVALID_PAYLOAD);
        }
        break;

    /* Time sync */
    case MSG_TIMESYNC_PULSE:
        clock_sync_on_pulse(&msg);
        break;

    /* Actuator commands */
    case MSG_ACTUATOR_CMD:
        handle_actuator_cmd(&msg);
        break;
    case MSG_COORDINATED_CMD:
        handle_coordinated_cmd(&msg);
        break;

    /* Stream control */
    case MSG_STREAM_OPEN: {
        nack_reason_t r = stream_open(&msg);
        if (r == NACK_UNKNOWN)
            protocol_send_ack(hdr.seq_num);
        else
            protocol_send_nack(hdr.seq_num, r);
        break;
    }
    case MSG_STREAM_DATA: {
        nack_reason_t r = stream_data(&msg);
        if (r != NACK_UNKNOWN)
            protocol_send_nack(hdr.seq_num, r);
        /* Stream data is not individually ack'd for throughput */
        break;
    }
    case MSG_STREAM_CLOSE: {
        nack_reason_t r = stream_close(&msg);
        if (r == NACK_UNKNOWN)
            protocol_send_ack(hdr.seq_num);
        else
            protocol_send_nack(hdr.seq_num, r);
        break;
    }

    /* Telemetry */
    case MSG_TELEMETRY_CONFIG:
        if (payload_len >= 2) {
            uint16_t interval = (uint16_t)payload[0]
                              | ((uint16_t)payload[1] << 8);
            telemetry_set_interval(interval);
            protocol_send_ack(hdr.seq_num);
        } else {
            protocol_send_nack(hdr.seq_num, NACK_INVALID_PAYLOAD);
        }
        break;
    case MSG_TELEMETRY_REQUEST:
        telemetry_send_full_state();
        protocol_send_ack(hdr.seq_num);
        break;

    /* System commands */
    case MSG_EMERGENCY_STOP:
        system_on_emergency_stop();
        protocol_send_ack(hdr.seq_num);
        break;
    case MSG_CLEAR_ESTOP:
        system_on_clear_emergency_stop();
        protocol_send_ack(hdr.seq_num);
        break;

    default:
        protocol_send_nack(hdr.seq_num, NACK_UNSUPPORTED_MSG);
        break;
    }
}
