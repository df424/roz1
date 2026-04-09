#include "handshake.h"
#include "protocol.h"
#include "framing.h"
#include <string.h>

static handshake_result_t g_result;

/* Our supported versions (highest first) */
static const uint8_t our_versions[] = { PROTOCOL_VERSION_CURRENT };
#define OUR_VERSION_COUNT (sizeof(our_versions) / sizeof(our_versions[0]))

/* Remote side's data */
static uint16_t remote_max_frame_size;
static uint8_t  remote_versions[8];
static uint8_t  remote_version_count;
static bool     remote_received;

void handshake_init(void)
{
    memset(&g_result, 0, sizeof(g_result));
    g_result.state = HS_IDLE;
    remote_received = false;
}

void handshake_start(void)
{
    g_result.state = HS_WAIT_REMOTE;
    remote_received = false;

    /* Send our VersionRequest */
    uint8_t payload[3 + OUR_VERSION_COUNT];
    payload[0] = (uint8_t)(FRAME_BUF_SIZE & 0xFF);
    payload[1] = (uint8_t)(FRAME_BUF_SIZE >> 8);
    payload[2] = OUR_VERSION_COUNT;
    memcpy(&payload[3], our_versions, OUR_VERSION_COUNT);

    protocol_send_msg(MSG_VERSION_REQUEST, payload, sizeof(payload));
}

static uint8_t compute_common_version(void)
{
    for (uint8_t i = 0; i < OUR_VERSION_COUNT; i++) {
        for (uint8_t j = 0; j < remote_version_count; j++) {
            if (our_versions[i] == remote_versions[j])
                return our_versions[i];
        }
    }
    return 0;  /* no common version */
}

void handshake_on_version_request(const protocol_message_t *msg)
{
    if (msg->payload_len < 3) {
        g_result.state = HS_FAILED;
        return;
    }

    remote_max_frame_size = (uint16_t)msg->payload[0]
                          | ((uint16_t)msg->payload[1] << 8);
    remote_version_count = msg->payload[2];
    if (remote_version_count > sizeof(remote_versions))
        remote_version_count = sizeof(remote_versions);
    if (msg->payload_len < 3u + remote_version_count) {
        g_result.state = HS_FAILED;
        return;
    }
    memcpy(remote_versions, &msg->payload[3], remote_version_count);
    remote_received = true;

    protocol_send_ack(msg->header->seq_num);

    /* Compute selected version and send VersionConfirm */
    uint8_t selected = compute_common_version();
    uint16_t eff_frame = FRAME_BUF_SIZE;
    if (remote_max_frame_size < eff_frame)
        eff_frame = remote_max_frame_size;

    g_result.selected_version = selected;
    g_result.effective_frame_size = eff_frame;

    uint8_t confirm_payload[3];
    confirm_payload[0] = selected;
    confirm_payload[1] = (uint8_t)(eff_frame & 0xFF);
    confirm_payload[2] = (uint8_t)(eff_frame >> 8);
    protocol_send_msg(MSG_VERSION_CONFIRM, confirm_payload,
                      sizeof(confirm_payload));

    g_result.state = HS_WAIT_CONFIRM;
}

void handshake_on_version_confirm(const protocol_message_t *msg)
{
    if (msg->payload_len < 3) {
        g_result.state = HS_FAILED;
        return;
    }

    uint8_t their_selected = msg->payload[0];
    protocol_send_ack(msg->header->seq_num);

    if (their_selected == 0 || their_selected != g_result.selected_version) {
        g_result.state = HS_FAILED;
        return;
    }

    g_result.state = HS_COMPLETE;
}

handshake_state_t handshake_get_state(void)
{
    return g_result.state;
}

const handshake_result_t *handshake_result(void)
{
    return &g_result;
}
