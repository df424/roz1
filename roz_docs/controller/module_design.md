# Robotic Head Controller - Module & Type Design

## 1. Design Principles

- **No dynamic allocation.** All buffers, queues, and state are statically allocated. The system must be fully determinable at compile time.
- **No RTOS.** The controller runs a cooperative bare-metal main loop. Each module exposes a `_tick()` function called every iteration. No blocking waits except during startup sequences.
- **Hardware abstraction at the bottom.** A thin platform abstraction layer (PAL) isolates all hardware access. Porting to a new MCU means reimplementing the PAL; everything above it is unchanged.
- **Modules own their state.** Each module manages its own static data. Cross-module communication happens through function calls and shared type definitions, not global variables.
- **Const configuration, mutable state.** Device configuration (actuator limits, homing behavior, etc.) lives in `const` tables in flash. Runtime state lives in RAM.

---

## 2. System States

The controller operates as a top-level state machine:

```
                    ┌──────┐
                    │ BOOT │
                    └──┬───┘
                       │
                    ┌──▼───┐
                    │ POST │  (power-on self-test)
                    └──┬───┘
                       │
                 ┌─────▼──────┐
                 │ HANDSHAKE  │  (version negotiation)
                 └─────┬──────┘
                       │
                 ┌─────▼──────┐
          ┌──────│  RUNNING   │◄──────────────┐
          │      └─────┬──────┘               │
          │            │                      │
   ┌──────▼───────┐    │               ┌──────┴──────┐
   │ DISCONNECTED │    │               │ CLEAR_ESTOP │
   └──────┬───────┘    │               └─────────────┘
          │      ┌─────▼──────┐               ▲
          │      │   ESTOP    │───────────────┘
          │      └────────────┘
          │
          └──► (re-handshake on reconnect)
```

| State | Entry Condition | Behavior |
|---|---|---|
| BOOT | Power on / reset | Initialize hardware, configure peripherals |
| POST | BOOT complete | Test each device, report results via LED |
| HANDSHAKE | POST complete, or reconnect | Exchange version, wait for confirmation |
| RUNNING | Handshake success | Normal operation: accept commands, push telemetry |
| ESTOP | EmergencyStop received | Halt all actuators, reject commands, clear queues |
| DISCONNECTED | Comm timeout | Execute disconnect procedure, await reconnection |

---

## 3. Module Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              Application                                    │
│                        (main loop, state machine)                           │
├────────┬──────────┬─────────┬───────────┬──────────┬───────────┬───────────┤
│Actuator│  Stream  │  Sync   │ Telemetry │  System  │  Clock    │  Device   │
│Manager │  Manager │  Manager│  Manager  │  Manager │  Sync     │  Registry │
├────────┴──────────┴─────────┴───────────┴──────────┴───────────┴───────────┤
│                       Actuator Drivers                                      │
│          (servo_driver, stepper_driver, binary_driver, ...)                  │
├─────────────────────────────────────────────────────────────────────────────┤
│                           Protocol Layer                                    │
│                  (message parse/build, dispatch, handshake)                  │
├────────────────────────────────────────────────────────────────────────────-┤
│                           Framing Layer                                     │
│                  (COBS encode/decode, CRC-16, frame I/O)                    │
├────────────────────────────────────────────────────────────────────────────-┤
│                     Platform Abstraction Layer (PAL)                         │
│            (transport, PWM, GPIO, system tick, input capture)                │
└─────────────────────────────────────────────────────────────────────────────┘
```

Dependencies flow strictly downward. No module may call into a module above it. Upward communication uses return values, flags, or callback registrations set during init.

---

## 4. Platform Abstraction Layer (PAL)

The PAL provides a C interface to hardware. Each subsystem is a separate header. The implementation files are the only place `#include "stm32l0xx_hal.h"` (or any vendor HAL) appears.

### 4.1 pal_transport

Abstract byte-oriented transport interface. The upper layers see a bidirectional byte pipe, not a UART.

```c
typedef struct {
    bool (*send)(const uint8_t *data, uint16_t len);
    uint16_t (*receive)(uint8_t *buf, uint16_t max_len);
    bool (*tx_busy)(void);
} pal_transport_t;

void pal_transport_init(pal_transport_t *tp);
```

A UART implementation fills this struct with functions that wrap `HAL_UART_Transmit`/`HAL_UART_Receive`. An SPI or USB implementation provides the same interface.

### 4.2 pal_pwm

Abstract PWM output channel. One instance per physical actuator output.

```c
typedef uint8_t pwm_channel_id_t;

void pal_pwm_init(void);
void pal_pwm_start(pwm_channel_id_t ch);
void pal_pwm_stop(pwm_channel_id_t ch);
void pal_pwm_set_pulse(pwm_channel_id_t ch, uint16_t pulse_us);
```

The implementation maps `pwm_channel_id_t` values to specific timer/channel combinations (e.g., channel 0 → TIM2 CH1, channel 3 → TIM22 CH2). This mapping is the only place hardware pin assignments appear.

### 4.3 pal_gpio

LED control and any future discrete I/O.

```c
void pal_gpio_init(void);
void pal_led_set(bool on);
void pal_led_toggle(void);
```

### 4.4 pal_tick

System time source. Wraps SysTick or a hardware timer.

```c
uint32_t pal_tick_ms(void);     // milliseconds since boot, wraps at ~49 days
uint32_t pal_tick_us(void);     // microseconds (if available), wraps faster
```

### 4.5 pal_capture

Hardware timer input capture for clock synchronization. A dedicated GPIO pin receives sync pulses from the companion computer. A timer input capture channel latches the counter value on the rising edge via interrupt -- no software jitter.

```c
void pal_capture_init(void);
bool pal_capture_ready(void);                   // new capture available?
uint32_t pal_capture_read_us(uint8_t *seq_out); // read captured timestamp, auto-increments seq
```

The implementation configures a timer channel in input capture mode and enables its interrupt. The ISR stores the captured value and sets a flag. `pal_capture_read_us` converts the raw timer count to microseconds and clears the flag.

The `seq` counter is incremented by the ISR on each capture, providing a sequence number to correlate with the TimeSyncPulse message from the SBC.

---

## 5. Framing Layer

Responsible for transforming raw bytes on the transport into validated frames, and vice versa.

### 5.1 cobs

Pure functions, no state. Operate on caller-provided buffers.

```c
uint16_t cobs_encode(const uint8_t *input, uint16_t len, uint8_t *output);
uint16_t cobs_decode(const uint8_t *input, uint16_t len, uint8_t *output);
```

### 5.2 crc16

```c
uint16_t crc16_ccitt(const uint8_t *data, uint16_t len);
```

### 5.3 framing

Manages frame accumulation from the transport (byte-by-byte until 0x00 delimiter), COBS decoding, CRC validation, and the reverse path for transmission.

```c
typedef struct {
    uint8_t raw[FRAME_BUF_SIZE];    // decoded frame (header + payload + crc)
    uint16_t len;                    // total decoded length
    bool valid;                      // CRC passed
} frame_t;

typedef struct {
    // RX accumulation buffer (COBS-encoded bytes before delimiter)
    uint8_t rx_accum[COBS_BUF_SIZE];
    uint16_t rx_accum_len;
    // TX scratch
    uint8_t tx_raw[FRAME_BUF_SIZE];
    uint8_t tx_cobs[COBS_BUF_SIZE];
} framing_state_t;

void framing_init(framing_state_t *state, pal_transport_t *transport);
bool framing_poll(framing_state_t *state, frame_t *out);   // returns true when a complete frame is ready
bool framing_send(framing_state_t *state, const uint8_t *header, const uint8_t *payload,
                  uint16_t payload_len);
```

`framing_poll` is called from the main loop. It reads available bytes from the transport, appends to the accumulation buffer, and when a 0x00 delimiter arrives, decodes and validates. If a valid frame is ready, it returns `true` and fills `out`.

---

## 6. Protocol Layer

Parses frame headers, serializes messages, and dispatches to handlers.

### 6.1 Types

```c
// --- Identifiers ---
typedef uint8_t  actuator_id_t;
typedef uint8_t  sensor_id_t;
typedef uint8_t  device_id_t;       // union of actuator_id and sensor_id in some contexts
typedef uint8_t  stream_id_t;
typedef uint16_t seq_num_t;
typedef uint16_t msg_type_t;
typedef uint16_t sync_tag_t;

// --- Frame Header ---
typedef struct {
    msg_type_t      msg_type;
    seq_num_t       seq_num;
    uint8_t         flags;
    uint16_t        payload_length;
    sync_tag_t      sync_tag;
} frame_header_t;

// --- Transfer type extraction ---
#define TRANSFER_TYPE(flags) ((flags) & 0x03)
#define TRANSFER_COMPLETE    0x00
#define TRANSFER_FRAGMENTED  0x01
#define TRANSFER_STREAM      0x02
```

### 6.2 Message Types (constants)

```c
// Protocol control
#define MSG_VERSION_REQUEST     0x0001
#define MSG_VERSION_CONFIRM     0x0002
#define MSG_ACK                 0x0010
#define MSG_NACK                0x0011
#define MSG_SYNC_EXECUTE        0x0020

// Time synchronization
#define MSG_TIMESYNC_PULSE      0x0030
#define MSG_TIMESYNC_REPORT     0x0031

// Actuator commands
#define MSG_ACTUATOR_CMD        0x0100
#define MSG_COORDINATED_CMD     0x0101

// Stream control
#define MSG_STREAM_OPEN         0x0200
#define MSG_STREAM_DATA         0x0201
#define MSG_STREAM_CLOSE        0x0202

// Telemetry
#define MSG_TELEMETRY_CONFIG    0x0300
#define MSG_TELEMETRY_REQUEST   0x0301
#define MSG_ACTUATOR_TELEMETRY  0x0310
#define MSG_SENSOR_TELEMETRY    0x0311
#define MSG_SYSTEM_TELEMETRY    0x0312
#define MSG_POST_RESULT         0x0320

// System commands
#define MSG_EMERGENCY_STOP      0x0400
#define MSG_CLEAR_ESTOP         0x0401
#define MSG_AUDIO_CLIP          0x0410
```

### 6.3 Nack Reason Codes

```c
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
```

### 6.4 protocol_dispatch

Central message router. Called by the application when a valid frame arrives.

```c
typedef struct {
    const frame_header_t *header;
    const uint8_t *payload;
    uint16_t payload_len;
} protocol_message_t;

// Handler function signature
typedef void (*msg_handler_t)(const protocol_message_t *msg);

void protocol_init(void);
void protocol_dispatch(const frame_t *frame);

// Outgoing helpers
void protocol_send_ack(seq_num_t ref_seq);
void protocol_send_nack(seq_num_t ref_seq, nack_reason_t reason);
seq_num_t protocol_next_seq(void);
```

`protocol_dispatch` parses the header, checks the sync_tag (forwarding to sync_manager if non-zero), and routes to the appropriate handler based on msg_type range.

### 6.5 handshake

Version negotiation state machine.

```c
typedef enum {
    HS_IDLE,
    HS_WAIT_REMOTE,     // sent our VersionRequest, waiting for theirs
    HS_WAIT_CONFIRM,    // computed version, sent VersionConfirm, waiting for theirs
    HS_COMPLETE,
    HS_FAILED,
} handshake_state_t;

typedef struct {
    handshake_state_t state;
    uint8_t selected_version;
    uint16_t effective_frame_size;
} handshake_result_t;

void handshake_init(void);
void handshake_start(void);                            // send VersionRequest
void handshake_on_version_request(const protocol_message_t *msg);
void handshake_on_version_confirm(const protocol_message_t *msg);
handshake_state_t handshake_state(void);
const handshake_result_t *handshake_result(void);
```

---

## 7. Device Registry

Static, compile-time table of all actuators and sensors on this robot. This is the single place where the hardware configuration is defined.

### 7.1 Actuator Configuration Types

```c
typedef enum {
    ACTUATOR_SERVO,             // standard positional servo
    ACTUATOR_LINEAR,            // linear actuator
    ACTUATOR_CONTINUOUS,        // continuous rotation
    ACTUATOR_BINARY,            // on/off (e.g., solenoid)
} actuator_type_t;

typedef enum {
    HOMING_NONE,                // no action on startup
    HOMING_RETURN_TO_DEFAULT,   // move to default_position
    HOMING_CALIBRATION,         // run calibration sequence
} homing_behavior_t;

typedef enum {
    HOLD_ACTIVE,                // maintain position (servo holding torque)
    HOLD_PASSIVE,               // de-energize when idle
} hold_behavior_t;

typedef struct actuator_driver actuator_driver_t;  // forward decl (see Section 8)

typedef struct {
    actuator_id_t               id;
    actuator_type_t             type;
    homing_behavior_t           homing;
    hold_behavior_t             hold;
    float                       min_position;       // lower mechanical limit
    float                       max_position;       // upper mechanical limit
    float                       default_position;   // homing target / startup position
    const actuator_driver_t     *driver;            // type-specific driver (see Section 8)
    const void                  *driver_config;     // opaque, interpreted by driver
} actuator_config_t;
```

### 7.2 Sensor Configuration Types

```c
typedef enum {
    SENSOR_AUDIO_INPUT,
    SENSOR_VIDEO_INPUT,
    SENSOR_POSITION_FEEDBACK,
    SENSOR_TEMPERATURE,
} sensor_type_t;

typedef struct {
    sensor_id_t         id;
    sensor_type_t       type;
    uint16_t            sample_rate_hz;     // 0 if not applicable
} sensor_config_t;
```

### 7.3 Registry Interface

```c
// Defined in a configuration source file (e.g., robot_config.c)
extern const actuator_config_t ACTUATOR_TABLE[];
extern const uint8_t ACTUATOR_COUNT;

extern const sensor_config_t SENSOR_TABLE[];
extern const uint8_t SENSOR_COUNT;

// Lookup helpers
const actuator_config_t *registry_find_actuator(actuator_id_t id);
const sensor_config_t *registry_find_sensor(sensor_id_t id);
```

### 7.4 Example Configuration

```c
// robot_config.c -- reference hardware

// Driver-specific config for each servo (see Section 8)
static const servo_config_t servo_neck   = { .pwm_channel = 0, .min_pulse_us = 1000, .max_pulse_us = 2000 };
static const servo_config_t servo_eye_h  = { .pwm_channel = 1, .min_pulse_us = 1000, .max_pulse_us = 2000 };
static const servo_config_t servo_eye_v  = { .pwm_channel = 2, .min_pulse_us = 1000, .max_pulse_us = 2000 };
static const servo_config_t servo_jaw    = { .pwm_channel = 3, .min_pulse_us = 1000, .max_pulse_us = 2000 };

const actuator_config_t ACTUATOR_TABLE[] = {
    { .id = 0, .type = ACTUATOR_SERVO, .homing = HOMING_RETURN_TO_DEFAULT,
      .hold = HOLD_ACTIVE, .min_position = 0.0f, .max_position = 1.0f,
      .default_position = 0.5f,
      .driver = &servo_driver, .driver_config = &servo_neck },      // neck yaw

    { .id = 1, .type = ACTUATOR_SERVO, .homing = HOMING_RETURN_TO_DEFAULT,
      .hold = HOLD_ACTIVE, .min_position = 0.0f, .max_position = 1.0f,
      .default_position = 0.5f,
      .driver = &servo_driver, .driver_config = &servo_eye_h },     // eye horizontal

    { .id = 2, .type = ACTUATOR_SERVO, .homing = HOMING_RETURN_TO_DEFAULT,
      .hold = HOLD_ACTIVE, .min_position = 0.0f, .max_position = 1.0f,
      .default_position = 0.5f,
      .driver = &servo_driver, .driver_config = &servo_eye_v },     // eye vertical

    { .id = 3, .type = ACTUATOR_SERVO, .homing = HOMING_RETURN_TO_DEFAULT,
      .hold = HOLD_PASSIVE, .min_position = 0.0f, .max_position = 1.0f,
      .default_position = 0.0f,
      .driver = &servo_driver, .driver_config = &servo_jaw },       // jaw
};
const uint8_t ACTUATOR_COUNT = 4;
```

---

## 8. Actuator Drivers

The actuator driver layer sits between the actuator manager and the PAL. Each driver knows how to translate a normalized position into hardware-specific actions for one type of actuator. The actuator manager owns queues and interpolation; drivers own hardware output.

### 8.1 Driver Interface

```c
typedef struct actuator_driver {
    void (*init)(const actuator_config_t *cfg);
    void (*set_position)(const actuator_config_t *cfg, float position);
    void (*enable)(const actuator_config_t *cfg);       // activate output (hold torque, energize coil)
    void (*disable)(const actuator_config_t *cfg);      // de-energize (passive release)
} actuator_driver_t;
```

Each actuator in the device registry carries a `const actuator_driver_t *driver` and a `const void *driver_config`. The manager calls through these function pointers without knowing the underlying hardware.

- `init` -- called once during startup. Configures the hardware channel.
- `set_position` -- called every tick while the actuator is moving. Maps the position (within the actuator's `min_position`..`max_position` range) to the appropriate hardware output.
- `enable` -- activates the output. Called when the actuator begins moving or enters active hold.
- `disable` -- de-energizes the output. Called when the actuator enters passive release (per hold behavior) or on emergency stop.

### 8.2 Servo Driver

The servo driver maps a normalized position to a PWM pulse width via `pal_pwm`.

```c
typedef struct {
    pwm_channel_id_t    pwm_channel;
    uint16_t            min_pulse_us;       // pulse width at min_position (e.g., 1000)
    uint16_t            max_pulse_us;       // pulse width at max_position (e.g., 2000)
} servo_config_t;

// Global driver instance -- all servos share this vtable
extern const actuator_driver_t servo_driver;
```

**Position-to-pulse mapping:**

```
pulse_us = min_pulse_us + (position - min_position) / (max_position - min_position)
                          × (max_pulse_us - min_pulse_us)
```

Implementation:

```c
static void servo_set_position(const actuator_config_t *cfg, float position) {
    const servo_config_t *sc = cfg->driver_config;
    float normalized = (position - cfg->min_position) / (cfg->max_position - cfg->min_position);
    uint16_t pulse = sc->min_pulse_us + (uint16_t)(normalized * (sc->max_pulse_us - sc->min_pulse_us));
    pal_pwm_set_pulse(sc->pwm_channel, pulse);
}

static void servo_enable(const actuator_config_t *cfg) {
    const servo_config_t *sc = cfg->driver_config;
    pal_pwm_start(sc->pwm_channel);
}

static void servo_disable(const actuator_config_t *cfg) {
    const servo_config_t *sc = cfg->driver_config;
    pal_pwm_stop(sc->pwm_channel);
}

const actuator_driver_t servo_driver = {
    .init         = servo_init,
    .set_position = servo_set_position,
    .enable       = servo_enable,
    .disable      = servo_disable,
};
```

### 8.3 Future Drivers

| Driver | Hardware | PAL dependency | Notes |
|---|---|---|---|
| `servo_driver` | PWM servo | `pal_pwm` | Reference implementation |
| `stepper_driver` | Stepper motor | `pal_gpio` (step/dir pins) | Position = step count, driver manages step timing |
| `binary_driver` | Solenoid / relay | `pal_gpio` | Position threshold → on/off |
| `linear_driver` | Linear actuator w/ feedback | `pal_pwm` + `pal_adc` | PID loop inside `set_position` |

Adding a new actuator type means writing a new driver source file and a corresponding config struct. No changes to the actuator manager, protocol layer, or registry interface.

---

## 9. Actuator Manager

Owns per-actuator runtime state, command queues, and interpolation. Delegates all hardware output to the actuator driver layer (Section 8). The manager has no knowledge of PWM, GPIO, or any specific actuator hardware.

### 9.1 Runtime State

```c
typedef enum {
    ACT_STATUS_IDLE,
    ACT_STATUS_MOVING,
    ACT_STATUS_HOLDING,
    ACT_STATUS_HOMING,
    ACT_STATUS_CALIBRATING,
    ACT_STATUS_FAULT,
    ACT_STATUS_ESTOP,
} actuator_status_t;

typedef enum {
    ACTION_QUEUE    = 0x00,
    ACTION_OVERRIDE = 0x01,
} action_mode_t;

typedef struct {
    float           target_position;
    float           speed;              // units per second
    action_mode_t   action_mode;
} actuator_command_t;

typedef struct {
    actuator_status_t   status;
    float               current_position;
    float               target_position;
    float               speed;              // current interpolation speed
    uint8_t             fault_code;

    // command queue (circular buffer)
    actuator_command_t  queue[ACTUATOR_QUEUE_DEPTH];
    uint8_t             queue_head;
    uint8_t             queue_tail;
    uint8_t             queue_count;
} actuator_state_t;
```

### 9.2 Interface

```c
#define ACTUATOR_QUEUE_DEPTH 4      // tunable per RAM budget

void actuator_manager_init(void);
void actuator_manager_tick(uint32_t now_ms);

// Command interface (called by protocol dispatch)
nack_reason_t actuator_command(actuator_id_t id, float position, float speed,
                               action_mode_t mode);
nack_reason_t actuator_coordinated_command(const actuator_command_t *cmds,
                                            const actuator_id_t *ids,
                                            uint8_t count);

// State queries (called by telemetry)
const actuator_state_t *actuator_get_state(actuator_id_t id);

// System-level
void actuator_emergency_stop(void);         // halt all, clear queues
void actuator_clear_emergency_stop(void);   // resume, hold current positions
void actuator_homing_start(void);           // initiate startup homing sequence
bool actuator_homing_complete(void);        // all actuators done homing?
```

### 9.3 Interpolation

`actuator_manager_tick` is called every main loop iteration with the current timestamp. For each actuator that is MOVING:

1. Compute elapsed time since last tick.
2. Compute step = speed × dt.
3. Move `current_position` toward `target_position` by step, clamping at target.
4. Call `cfg->driver->set_position(cfg, current_position)` -- the driver translates to hardware.
5. If `current_position == target_position`, dequeue next command or transition to IDLE/HOLDING.

On transition to IDLE/HOLDING, the manager consults the actuator's hold behavior:
- `HOLD_ACTIVE` → call `cfg->driver->enable(cfg)` (if not already enabled), status = HOLDING.
- `HOLD_PASSIVE` → call `cfg->driver->disable(cfg)`, status = IDLE.

On emergency stop, the manager calls `cfg->driver->disable(cfg)` for every actuator regardless of hold behavior.

### 9.4 Coordinated Motion

For a coordinated command group, `actuator_coordinated_command` validates all commands first (checking limits, IDs). If any fail, the entire group is rejected. If all pass, commands are applied atomically -- each actuator receives its command before any `_tick` processing.

---

## 10. Stream Manager

Manages the lifecycle of active streams (audio in, audio out, video).

```c
#define MAX_CONCURRENT_STREAMS 2

typedef struct {
    stream_id_t     id;
    uint8_t         stream_type;    // from StreamOpen
    device_id_t     device_id;
    uint16_t        sample_rate;
    uint8_t         sample_format;
    bool            active;
} stream_state_t;

void stream_manager_init(void);

nack_reason_t stream_open(const protocol_message_t *msg);
nack_reason_t stream_data(const protocol_message_t *msg);
nack_reason_t stream_close(const protocol_message_t *msg);

void stream_close_all(void);           // called on emergency stop / disconnect
uint8_t stream_active_count(void);
```

Stream data processing is immediate and incremental -- audio samples are forwarded to a PAL audio output buffer (future), not accumulated in RAM.

---

## 11. Sync Manager

Buffers messages tagged with a non-zero `sync_tag` and executes them atomically when `SyncExecute` arrives.

```c
#define MAX_SYNC_GROUPS     2
#define SYNC_BUFFER_SIZE    128     // bytes, shared across all pending groups

typedef struct {
    sync_tag_t  tag;
    uint8_t     buffer[SYNC_BUFFER_SIZE];
    uint16_t    used;
    uint8_t     msg_count;
    uint32_t    timestamp_ms;       // for timeout
} sync_group_t;

void sync_manager_init(void);
void sync_manager_tick(uint32_t now_ms);    // check for timeouts

// Called by protocol_dispatch when sync_tag != 0
nack_reason_t sync_buffer_message(sync_tag_t tag, const protocol_message_t *msg);

// Called when SyncExecute arrives
nack_reason_t sync_execute(sync_tag_t tag);
```

`sync_execute` replays each buffered message through `protocol_dispatch` with `sync_tag` set to 0 (so they execute immediately this time). After execution, the group is freed.

---

## 12. Telemetry Manager

Builds and sends periodic telemetry reports. Also handles on-demand full state dumps.

```c
void telemetry_init(void);
void telemetry_tick(uint32_t now_ms);

void telemetry_set_interval(uint16_t interval_ms);     // 0 = disabled
void telemetry_send_full_state(void);                   // immediate dump (all three reports)
void telemetry_send_post_result(void);                  // send POST results
```

`telemetry_tick` checks if the push interval has elapsed. If so, it builds ActuatorTelemetry, SensorTelemetry, and SystemTelemetry messages by querying the actuator manager, sensor state, and system manager, then sends them through the protocol/framing layers.

Telemetry messages include a `controller_time_us` field (from `pal_tick_us()`) stamped at the moment of sampling, enabling the SBC to map reports to its own clock via the offset established by the clock sync module.

---

## 13. Clock Sync

Manages hardware-assisted clock synchronization between the controller and companion computer (CTRL-R30). Uses the PAL input capture to timestamp sync pulses with cycle-accurate precision.

```c
typedef struct {
    uint32_t    capture_us;     // controller timestamp from input capture
    uint8_t     pulse_seq;      // sequence number from ISR
    bool        pending;        // capture waiting for matching TimeSyncPulse
} clock_sync_state_t;

void clock_sync_init(void);
void clock_sync_tick(uint32_t now_ms);  // check for unmatched capture timeout

// Called by protocol_dispatch when TimeSyncPulse arrives
void clock_sync_on_pulse(const protocol_message_t *msg);
```

**Flow within the controller:**

1. SBC drives a rising edge on the sync GPIO line.
2. `pal_capture` ISR fires, latches timer value, increments `pulse_seq`, sets `pending = true`.
3. SBC sends TimeSyncPulse message containing its own timestamp and `pulse_seq`.
4. `clock_sync_on_pulse` matches the `pulse_seq`, pairs it with the stored `capture_us`, and sends a TimeSyncReport message back with `{ capture_us, pulse_seq }`.
5. If `pending` is true for too long without a matching TimeSyncPulse, `clock_sync_tick` discards the stale capture.

The controller does not need to compute the clock offset itself -- the SBC has both timestamps and computes the offset. The controller's only job is to capture the pulse accurately and report the timestamp.

---

## 14. System Manager

Top-level state machine, emergency stop logic, POST, CBIT, disconnect detection, and LED indication.

```c
typedef enum {
    SYS_BOOT,
    SYS_POST,
    SYS_HANDSHAKE,
    SYS_RUNNING,
    SYS_ESTOP,
    SYS_DISCONNECTED,
} system_state_t;

typedef struct {
    system_state_t  state;
    uint32_t        boot_time_ms;
    uint32_t        last_rx_time_ms;        // for disconnect detection
    uint16_t        disconnect_timeout_ms;
    uint8_t         led_pattern;
} system_context_t;

void system_init(void);
void system_tick(uint32_t now_ms);

system_state_t system_get_state(void);

void system_on_frame_received(void);        // resets disconnect timer
void system_on_emergency_stop(void);
void system_on_clear_emergency_stop(void);
```

### 14.1 POST Sequence

During `SYS_POST`, the system manager iterates over the device registry and tests each device:
- **Actuators**: Start PWM, command a small movement, verify no fault. (Implementation-specific; may just verify PWM output starts without error.)
- **Sensors**: Verify device responds (e.g., read a sample, check for timeout).

Results are stored in a static array and sent via `telemetry_send_post_result` after handshake completes.

### 14.2 CBIT

During `SYS_RUNNING`, `system_tick` monitors:
- Actuator stall detection (position not changing despite active command -- delegated to actuator manager).
- Communication timeout (no valid frame within `disconnect_timeout_ms`).
- Any fault flags set by other modules.

### 14.3 LED Patterns

| System State | LED Pattern |
|---|---|
| BOOT | Solid on |
| POST | Fast blink |
| HANDSHAKE | Slow blink |
| RUNNING | Heartbeat (short flash every 2s) |
| ESTOP | Rapid flash |
| DISCONNECTED | Double blink |
| Fault | SOS pattern or steady fast blink |

---

## 15. Application (Main Loop)

The application module lives in `main.c` within USER CODE blocks. It wires all modules together.

### 15.1 Initialization Sequence

```
1. HAL_Init(), SystemClock_Config(), MX_*_Init()    [CubeMX generated]
2. pal_init()                                        [PAL layer, including pal_capture_init]
3. device_registry (static, no init needed)
4. framing_init()
5. protocol_init()
6. actuator_manager_init()
7. stream_manager_init()
8. sync_manager_init()
9. clock_sync_init()
10. telemetry_init()
11. system_init()                                    [enters SYS_POST]
12. actuator_homing_start()                          [POST + homing]
13. Wait for homing complete → SYS_HANDSHAKE
14. handshake_start()                                [send VersionRequest]
15. Main loop begins
```

### 15.2 Main Loop

```c
while (1) {
    uint32_t now = pal_tick_ms();

    // 1. Receive: poll transport for incoming frames
    if (framing_poll(&framing, &rx_frame)) {
        system_on_frame_received();
        protocol_dispatch(&rx_frame);
    }

    // 2. Execute: advance actuator interpolation
    actuator_manager_tick(now);

    // 3. Monitor: check timeouts, CBIT, LED, clock sync
    sync_manager_tick(now);
    clock_sync_tick(now);
    system_tick(now);

    // 4. Report: send telemetry if due
    telemetry_tick(now);
}
```

Every iteration: receive → execute → monitor → report. No blocking, no waits. Each `_tick` function returns quickly (microseconds).

---

## 16. Data Flow

### 16.1 Incoming Command (e.g., ActuatorCommand)

```
Transport (bytes)
  → framing_poll() → decoded frame_t
    → protocol_dispatch() → parse header, check sync_tag
      → [sync_tag != 0] → sync_buffer_message()
      → [sync_tag == 0] → actuator_command()
        → validate limits → enqueue or override
        → protocol_send_ack() or protocol_send_nack()
          → framing_send() → COBS encode → transport send
```

### 16.2 Outgoing Telemetry

```
telemetry_tick() → interval elapsed?
  → query actuator_get_state() for each actuator
  → build ActuatorTelemetry payload
  → framing_send() → COBS encode → transport send
```

### 16.3 Synchronized Execution

```
Message with sync_tag=42 arrives
  → sync_buffer_message(42, msg) → stored in sync group
Another message with sync_tag=42 arrives
  → sync_buffer_message(42, msg) → appended
SyncExecute(42) arrives
  → sync_execute(42) → replay each buffered msg through dispatch with tag=0
    → actuator_command(), stream_data(), etc. execute immediately
  → protocol_send_ack()
```

---

## 17. Memory Budget (Reference MCU: 8KB RAM)

| Allocation | Size | Notes |
|---|---|---|
| Stack | 1024 B | As configured in linker script |
| Heap | 512 B | Unused (no malloc), but reserved by linker |
| Framing RX accum | 280 B | Max COBS-encoded frame + margin |
| Framing TX raw | 256 B | Max raw frame |
| Framing TX COBS | 280 B | COBS-encoded output |
| Frame decode scratch | 256 B | Decoded frame for processing |
| Actuator state × 4 | 240 B | ~60 B each (state + queue of 4 commands) |
| Sync groups × 2 | 268 B | 2 groups × (tag + 128 B buffer + metadata) |
| Stream state × 2 | 32 B | Metadata only, no data buffering |
| System context | 32 B | State machine, timers |
| Telemetry scratch | — | Reuses TX raw buffer |
| Handshake state | 16 B | |
| POST results | 24 B | 8 devices × 3 bytes |
| Clock sync state | 12 B | Capture timestamp, seq, pending flag |
| **Subtotal (app)** | **~3,200 B** | |
| HAL/BSP globals | ~800 B | Timer handles, UART handle, etc. |
| `.bss` padding, alignment | ~200 B | |
| **Total estimated** | **~4,200 B** | Leaves ~3,800 B margin |

The budget is tight but viable. Key tradeoffs:
- `ACTUATOR_QUEUE_DEPTH` of 4 keeps queues small. Can reduce to 2 if needed.
- `SYNC_BUFFER_SIZE` of 128 bytes limits sync group complexity. Sufficient for 2-3 small commands per group.
- `MAX_CONCURRENT_STREAMS` of 2 is enough for audio out + audio in. Stream data is not buffered.
- Fragment reassembly is **not budgeted** -- fragmented transfers (e.g., audio clips) may require either a larger MCU or offloading to the companion computer.

---

## 18. File Organization

```
Src/
├── main.c                      [CubeMX generated, application in USER CODE blocks]
├── stm32l0xx_hal_msp.c         [CubeMX generated, pin mappings]
├── stm32l0xx_it.c              [CubeMX generated, interrupt handlers]
│
├── pal/
│   ├── pal_transport_uart.c    [UART implementation of pal_transport]
│   ├── pal_pwm_stm32l0.c      [STM32L0 timer/PWM implementation]
│   ├── pal_gpio_stm32l0.c     [STM32L0 GPIO implementation]
│   ├── pal_tick_stm32l0.c     [SysTick wrapper]
│   └── pal_capture_stm32l0.c  [Timer input capture for clock sync]
│
├── framing/
│   ├── cobs.c
│   ├── crc16.c
│   └── framing.c
│
├── protocol/
│   ├── protocol_dispatch.c
│   └── handshake.c
│
├── drivers/
│   ├── servo_driver.c          [servo: position → PWM pulse via pal_pwm]
│   ├── stepper_driver.c        [future: stepper motor via pal_gpio]
│   └── binary_driver.c         [future: on/off actuator via pal_gpio]
│
├── actuator_manager.c
├── stream_manager.c
├── sync_manager.c
├── clock_sync.c
├── telemetry.c
├── system_manager.c
└── robot_config.c              [device registry tables for this specific robot]

Inc/
├── main.h                      [CubeMX generated]
│
├── pal/
│   ├── pal_transport.h
│   ├── pal_pwm.h
│   ├── pal_gpio.h
│   ├── pal_tick.h
│   └── pal_capture.h
│
├── framing/
│   ├── cobs.h
│   ├── crc16.h
│   └── framing.h
│
├── protocol/
│   ├── protocol.h              [message types, header struct, nack codes]
│   ├── protocol_dispatch.h
│   └── handshake.h
│
├── drivers/
│   ├── actuator_driver.h       [actuator_driver_t interface definition]
│   ├── servo_driver.h          [servo_config_t, extern servo_driver]
│   ├── stepper_driver.h        [future]
│   └── binary_driver.h         [future]
│
├── actuator.h                  [actuator types, config, state, command structs]
├── sensor.h                    [sensor types, config, state]
├── device_registry.h
├── actuator_manager.h
├── stream_manager.h
├── sync_manager.h
├── clock_sync.h
├── telemetry.h
└── system_manager.h
```

CubeMX-generated files remain at the top level of `Src/` and `Inc/`. Application modules go in subdirectories. The build system (`Debug/Src/subdir.mk`) will need entries for the new subdirectories.

---

## 19. Module Dependency Graph

```
                       ┌──────────────────┐
                       │   main.c (app)   │
                       └┬──┬──┬──┬──┬──┬─┘
                        │  │  │  │  │  │
       ┌────────────────┘  │  │  │  │  └────────────────┐
       │     ┌─────────────┘  │  │  └──────────┐        │
       │     │     ┌──────────┘  └───────┐     │        │
       ▼     ▼     ▼                     ▼     ▼        ▼
  ┌────────┐ ┌──────────┐ ┌──────────┐ ┌────────┐ ┌────────┐
  │actuator│ │  stream  │ │telemetry │ │ system │ │  sync  │
  │manager │ │  manager │ │ manager  │ │manager │ │manager │
  └──┬─────┘ └────┬─────┘ └──┬───┬───┘ └──┬──┬──┘ └───┬────┘
     │            │          │   │        │  │        │
     ▼            │      ┌───┘   │   ┌────┘  │        │
  ┌──────────────┐│      │       │   │       │        │
  │  actuator    ││      │       │   │       │        │
  │  drivers     ││      │       │   │       │        │
  │  (servo,etc.)││      │       │   │       │        │
  └──┬───────┬───┘│      │       │   │       │        │
     │       │    │      │       │   │       │        │
     │       │┌───┘      │       │   │       │        │
     │       ││          │       │   │       │        │
     │       ▼▼          ▼       │   │       │        │
     │  ┌──────────────────────┐ │   │       │        │
     │  │  protocol layer      │◄┘   │       │        │
     │  │  (dispatch, handshake│◄────┘       │        │
     │  │   ack/nack, build)   │◄────────────┘        │
     │  └──────────┬───────────┘                      │
     │             │    ▲    ▲                        │
     │             ▼    │    │                        │
     │  ┌──────────────────┐ │  ┌─────────────┐       │
     │  │  framing layer   │ │  │ clock_sync  │       │
     │  │  (cobs, crc, I/O)│ │  └──┬──────┬───┘       │
     │  └────────┬─────────┘ │     │      │           │
     │           │           │     │      │           │
     │           ▼           │     │      ▼           │
     │  ┌──────────────────┐ │     │  ┌─────────────┐ │
     │  │  pal_transport   │ │     │  │ pal_capture │ │
     │  └──────────────────┘ │     │  └─────────────┘ │
     │                       │     │                  │
     ▼                       │     ▼                  │
  ┌──────────┐    ┌─────────────────┐                 │
  │pal_pwm,  │    │ device_registry │◄────────────────┘
  │pal_gpio  │    └─────────────────┘
  └──────────┘
```

Key: arrows point from **user** to **dependency**. The actuator manager delegates to actuator drivers, which are the only modules that touch PAL hardware output (pal_pwm, pal_gpio). The actuator manager itself has no hardware dependencies. The clock_sync module depends on both pal_capture (for hardware timestamps) and the protocol layer (for sending TimeSyncReport).
