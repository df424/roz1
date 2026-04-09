#ifndef PROTOCOL_DEFS_H
#define PROTOCOL_DEFS_H

#include <stdint.h>

/* --- Identifiers --- */
typedef uint8_t  actuator_id_t;
typedef uint8_t  sensor_id_t;
typedef uint8_t  device_id_t;
typedef uint8_t  stream_id_t;
typedef uint16_t seq_num_t;
typedef uint16_t msg_type_t;
typedef uint16_t sync_tag_t;

/* --- Frame Header (9 bytes, little-endian on wire) --- */
typedef struct {
    msg_type_t  msg_type;
    seq_num_t   seq_num;
    uint8_t     flags;
    uint16_t    payload_length;
    sync_tag_t  sync_tag;
} frame_header_t;

/* --- Transfer type extraction --- */
#define TRANSFER_TYPE(flags)  ((flags) & 0x03)
#define TRANSFER_COMPLETE     0x00
#define TRANSFER_FRAGMENTED   0x01
#define TRANSFER_STREAM       0x02

/* --- Protocol control --- */
#define MSG_VERSION_REQUEST     0x0001
#define MSG_VERSION_CONFIRM     0x0002
#define MSG_ACK                 0x0010
#define MSG_NACK                0x0011
#define MSG_SYNC_EXECUTE        0x0020

/* --- Time synchronization --- */
#define MSG_TIMESYNC_PULSE      0x0030
#define MSG_TIMESYNC_REPORT     0x0031

/* --- Actuator commands --- */
#define MSG_ACTUATOR_CMD        0x0100
#define MSG_COORDINATED_CMD     0x0101

/* --- Stream control --- */
#define MSG_STREAM_OPEN         0x0200
#define MSG_STREAM_DATA         0x0201
#define MSG_STREAM_CLOSE        0x0202

/* --- Telemetry --- */
#define MSG_TELEMETRY_CONFIG    0x0300
#define MSG_TELEMETRY_REQUEST   0x0301
#define MSG_ACTUATOR_TELEMETRY  0x0310
#define MSG_SENSOR_TELEMETRY    0x0311
#define MSG_SYSTEM_TELEMETRY    0x0312
#define MSG_POST_RESULT         0x0320

/* --- System commands --- */
#define MSG_EMERGENCY_STOP      0x0400
#define MSG_CLEAR_ESTOP         0x0401
#define MSG_AUDIO_CLIP          0x0410

/* --- Nack reason codes --- */
typedef enum {
    NACK_UNKNOWN              = 0x00,
    NACK_CRC_MISMATCH         = 0x01,
    NACK_UNSUPPORTED_MSG      = 0x02,
    NACK_INVALID_PAYLOAD      = 0x03,
    NACK_ACTUATOR_NOT_FOUND   = 0x04,
    NACK_SENSOR_NOT_FOUND     = 0x05,
    NACK_POSITION_OUT_OF_RANGE= 0x06,
    NACK_EMERGENCY_STOP       = 0x07,
    NACK_STREAM_NOT_FOUND     = 0x08,
    NACK_STREAM_IN_USE        = 0x09,
    NACK_MAX_STREAMS          = 0x0A,
    NACK_FRAGMENT_ERROR       = 0x0B,
    NACK_VERSION_MISMATCH     = 0x0C,
    NACK_FRAME_TOO_LARGE      = 0x0D,
    NACK_DEVICE_BUSY          = 0x0E,
    NACK_SYNC_TAG_TIMEOUT     = 0x0F,
} nack_reason_t;

/* --- Protocol message (parsed from frame) --- */
typedef struct {
    const frame_header_t *header;
    const uint8_t        *payload;
    uint16_t              payload_len;
} protocol_message_t;

/* --- Action modes --- */
typedef enum {
    ACTION_QUEUE    = 0x00,
    ACTION_OVERRIDE = 0x01,
} action_mode_t;

/* --- Supported protocol version --- */
#define PROTOCOL_VERSION_CURRENT  1

#endif
