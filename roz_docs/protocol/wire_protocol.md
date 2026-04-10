# ROZ - Communication Protocol

## 1. Overview

This document defines the binary communication protocol between embedded controllers and the base station (or companion computer). The protocol is transport-agnostic -- it defines message framing, integrity, and semantics independent of the physical link (UART, SPI, USB, etc.).

A robot may have multiple controllers (e.g., head, arm, leg), each communicating with the base station over an independent link. Each controller connection is a separate instance of this protocol. Controller identity is established during the version handshake (Section 5).

Both sides operate as asynchronous peers. Either side may send messages at any time without waiting for a response. Commands are acknowledged, telemetry is pushed periodically, and streams flow continuously.

---

## 2. Framing Layer

### 2.1 COBS Encoding

All frames are encoded using Consistent Overhead Byte Stuffing (COBS). COBS eliminates the zero byte (0x00) from the data, allowing 0x00 to serve as an unambiguous frame delimiter.

```
Wire format:
[COBS-encoded data] [0x00]
[COBS-encoded data] [0x00]
...
```

The COBS-encoded data, when decoded, yields the raw frame:

```
Raw frame:
[header (9 bytes)] [payload (0..N bytes)] [CRC16 (2 bytes)]
```

### 2.2 Maximum Frame Size

Each side advertises its maximum receive frame size (raw, pre-COBS) during the version handshake (Section 5). The sender must not exceed the receiver's advertised limit. COBS encoding expands data by at most 1 byte per 254, plus the 0x00 delimiter.

The controller's max frame size is constrained by available RAM. A typical value for the reference MCU is 256 bytes raw (yielding ~236 bytes of usable payload after header and CRC).

### 2.3 Integrity

Every raw frame ends with a CRC-16/CCITT checksum computed over the header and payload bytes. Polynomial: 0x1021, initial value: 0xFFFF.

```
CRC = CRC-16/CCITT(header || payload)
```

On receipt, the receiver computes the CRC over the header and payload of the decoded frame and compares it to the trailing two bytes. If the CRC does not match, the receiver sends a Nack referencing the sequence number from the corrupted header (if parseable) or drops the frame silently if the header is unreadable.

### 2.4 Byte Order

All multi-byte fields are little-endian.

---

## 3. Frame Header

Every frame begins with a 9-byte header:

```
Offset  Size    Field           Description
0       2       msg_type        Message type identifier (uint16)
2       2       seq_num         Sequence number (uint16)
4       1       flags           Bitfield (see below)
5       2       payload_length  Length of payload in bytes (uint16)
7       2       sync_tag        Synchronization tag (uint16, see Section 4.4)
```

### 3.1 Flags

```
Bit 0-1:  Transfer type
            00 = Complete message (single frame)
            01 = Fragmented message (multi-frame, known total size)
            10 = Stream data
            11 = Reserved
Bit 2-7:  Reserved (must be 0)
```

### 3.2 Sequence Numbers

Each side maintains its own monotonically increasing uint16 sequence counter, wrapping from 0xFFFF to 0x0000. Every frame sent increments the counter. Ack and Nack messages reference the sequence number of the frame they are responding to.

---

## 4. Transfer Modes

### 4.1 Complete Message

A single frame containing the entire message. Flags transfer type = `00`. This is the common case for commands, acknowledgments, and telemetry.

### 4.2 Fragmented Message

Used when a message exceeds the receiver's max frame size and the total size is known upfront (e.g., discrete audio clips, large configuration payloads). Flags transfer type = `01`.

Every fragment frame carries a fragmentation header at the start of the payload:

```
Offset  Size    Field             Description
0       2       fragment_index    Zero-based index of this fragment (uint16)
2       2       fragment_total    Total number of fragments (uint16)
4       ...     fragment_data     Payload data for this fragment
```

All fragments of a message share the same `msg_type`. The `seq_num` is unique per fragment (each fragment is independently ack'd/nack'd). The receiver reassembles fragments by `msg_type` and `fragment_total`, buffering until all fragments arrive. On receipt of the first fragment (index 0), the receiver knows the total count and can allocate accordingly.

If any fragment fails CRC or is not received within an implementation-defined timeout, the receiver may Nack the missing fragment or discard the entire fragmented transfer.

### 4.3 Stream

Used for continuous data of unknown or unbounded length (e.g., audio output streaming, audio input capture, video). Flags transfer type = `10`.

Streams are managed by three message types:

| Message | Purpose |
|---|---|
| StreamOpen | Establish a stream with a stream ID and format metadata |
| StreamData | Carry a chunk of stream data |
| StreamClose | Terminate the stream |

StreamOpen and StreamClose are complete messages (transfer type `00`) with their own message types. Only StreamData frames use transfer type `10`.

StreamData payload:

```
Offset  Size    Field         Description
0       1       stream_id     Identifies which open stream (uint8)
1       ...     stream_data   Raw stream bytes
```

The receiver processes stream data incrementally (e.g., feeding audio samples to a DAC buffer) and never needs to hold the entire stream in memory.

Multiple streams may be open simultaneously (e.g., audio input and video output from the controller). Each has a unique `stream_id` scoped to the direction of flow.

### 4.4 Synchronized Execution

Any message (command, stream data, etc.) may carry a non-zero `sync_tag` in its header. When the controller receives a message with a non-zero sync_tag, it buffers the message instead of executing it immediately. Buffered messages accumulate until the sender issues a SyncExecute message (see Section 7.7) with the same tag, at which point the controller executes all buffered messages with that tag atomically -- as if they arrived at the same instant.

A `sync_tag` of `0x0000` means "execute immediately" and is the default for all unsynchronized messages.

**Example: Synchronized speech and jaw movement**

```
Base Station sends:
  1. StreamData   [stream_id=1, sync_tag=42]  (audio chunk)
  2. ActuatorCommand [actuator_id=3 (jaw), sync_tag=42, position=0.7, speed=5.0]
  3. SyncExecute  [sync_tag=42]

Controller receives all three, then atomically:
  - Feeds the audio chunk to the speaker buffer
  - Begins moving the jaw to position 0.7

Next beat:
  4. StreamData   [stream_id=1, sync_tag=43]  (next audio chunk)
  5. ActuatorCommand [actuator_id=3 (jaw), sync_tag=43, position=0.3, speed=5.0]
  6. SyncExecute  [sync_tag=43]
```

**Semantics:**

- Sync tags are scoped per-direction (base station tags and controller tags are independent namespaces).
- A sync tag has no required relationship to sequence numbers -- it is chosen by the sender and only needs to be unique among currently pending (not yet flushed) sync groups.
- Synchronized actuator commands are implicitly Override mode -- they represent "be at this position for this moment," not a queued trajectory.
- The controller may impose a limit on the number of pending sync groups and the total buffered bytes. If exceeded, the controller Nacks the message with reason code 0x0E (device busy / queue full).
- If a SyncExecute arrives for a tag with no buffered messages, the controller Acks it as a no-op.
- If the sender abandons a sync group (never sends SyncExecute), the controller shall discard buffered messages for that tag after an implementation-defined timeout.

---

## 5. Version Handshake

The version handshake is the first exchange after the physical link is established. No other messages may be sent until the handshake completes.

### 5.1 Flow

```
Controller                          Base Station
    |                                    |
    |-------- VersionRequest ----------->|
    |<------- VersionRequest ------------|
    |                                    |
    |  (both compute highest common      |
    |   version independently)           |
    |                                    |
    |-------- VersionConfirm ----------->|
    |<------- VersionConfirm ------------|
    |                                    |
    |  (both verify confirms match)      |
    |                                    |
```

Both sides send VersionRequest simultaneously (or in either order -- the protocol is async). Both sides wait for the other's VersionRequest before computing the selected version and sending VersionConfirm.

### 5.2 VersionRequest Payload

```
Offset  Size    Field               Description
0       1       controller_id       Unique controller identifier (uint8, see 5.5)
1       2       max_frame_size      Maximum raw frame size this side can receive (uint16)
3       1       version_count       Number of supported versions (uint8)
4       N       versions            Array of supported protocol versions (uint8 each),
                                    ordered highest to lowest
```

### 5.3 VersionConfirm Payload

```
Offset  Size    Field               Description
0       1       selected_version    The highest common protocol version (uint8)
1       2       effective_frame_size  Min of both sides' max_frame_size (uint16)
3       1       controller_id       Echo of controller_id from VersionRequest (uint8)
```

### 5.4 Failure

If there is no common version (empty intersection), the VersionConfirm shall carry `selected_version = 0x00`, which is a reserved invalid version. Both sides enter a fault state and indicate the error via LED and/or any available diagnostic channel.

If the two VersionConfirm messages disagree on the selected version, both sides enter a fault state.

### 5.5 Controller Identity

The `controller_id` field identifies which controller is on the other end of the link. Each controller in a robot has a unique ID, defined at compile time in its device configuration. The base station uses this ID to associate the connection with the correct controller and its known set of actuators and sensors.

When the base station sends its own VersionRequest, it sets `controller_id` to `0x00` (base station). The controller's VersionRequest carries its configured ID (e.g., `0x01` for head, `0x02` for left arm). Both sides echo the controller's ID in VersionConfirm.

The `controller_id` value `0xFF` is reserved and must not be used.

---

## 6. Acknowledgment

### 6.1 Ack

Sent in response to a successfully received and accepted message.

```
Offset  Size    Field           Description
0       2       ref_seq_num     Sequence number of the acknowledged frame (uint16)
```

### 6.2 Nack

Sent in response to a rejected or corrupted message.

```
Offset  Size    Field           Description
0       2       ref_seq_num     Sequence number of the rejected frame (uint16)
2       1       reason          Reason code (uint8, see Section 10)
```

### 6.3 Retry Policy

The protocol defines the Nack mechanism. Retry behavior (how many times, backoff strategy) is implementation-defined and not part of this specification. Implementations should bound retries to avoid infinite loops on persistent errors.

---

## 7. Message Catalog

### 7.1 Message Type Ranges

```
Range           Category
0x0000-0x00FF   Protocol control (handshake, ack, nack)
0x0100-0x01FF   Actuator commands
0x0200-0x02FF   Stream control
0x0300-0x03FF   Telemetry and status
0x0400-0x04FF   System commands
0x0500-0xFFFE   Reserved for future use
0xFFFF          Reserved (invalid)
```

### 7.2 Protocol Control Messages (0x0000-0x00FF)

| Type ID | Name | Direction | Description |
|---|---|---|---|
| 0x0001 | VersionRequest | Bidirectional | Initiate version handshake |
| 0x0002 | VersionConfirm | Bidirectional | Confirm selected version |
| 0x0010 | Ack | Bidirectional | Acknowledge a received frame |
| 0x0011 | Nack | Bidirectional | Reject a received frame |
| 0x0020 | SyncExecute | Bidirectional | Flush and execute a sync group (see Section 7.6) |
| 0x0030 | TimeSyncPulse | BS -> Ctrl | Report SBC timestamp of a hardware sync pulse (see Section 7.7) |
| 0x0031 | TimeSyncReport | Ctrl -> BS | Report controller's input-captured timestamp for the pulse |
| 0x0032 | SoftSyncRequest | BS -> Ctrl | Software clock sync request with host timestamp (see Section 7.7) |
| 0x0033 | SoftSyncResponse | Ctrl -> BS | Software clock sync response with controller timestamp |

### 7.3 Actuator Commands (0x0100-0x01FF)

| Type ID | Name | Direction | Description |
|---|---|---|---|
| 0x0100 | ActuatorCommand | BS -> Ctrl | Command a single actuator |
| 0x0101 | CoordinatedCommand | BS -> Ctrl | Command multiple actuators as a synchronized group |

**ActuatorCommand Payload:**

```
Offset  Size    Field           Description
0       1       actuator_id     Target actuator identifier (uint8)
1       1       action_mode     0x00 = Queue, 0x01 = Override (uint8)
2       4       target_position Position in actuator-native units (float32)
6       4       speed           Movement speed in units/second (float32)
```

**CoordinatedCommand Payload:**

```
Offset  Size    Field           Description
0       1       count           Number of actuator commands in group (uint8)
1       10*N    commands        Array of actuator commands, each:
                                  actuator_id (uint8)
                                  action_mode (uint8)
                                  target_position (float32)
                                  speed (float32)
```

All actuators in a coordinated command group use the same action mode. If action modes differ within a group, the controller shall Nack the entire command.

### 7.4 Stream Control Messages (0x0200-0x02FF)

| Type ID | Name | Direction | Description |
|---|---|---|---|
| 0x0200 | StreamOpen | Bidirectional | Open a new stream |
| 0x0201 | StreamData | Bidirectional | Carry stream payload (transfer type = `10`) |
| 0x0202 | StreamClose | Bidirectional | Close an open stream |

**StreamOpen Payload:**

```
Offset  Size    Field           Description
0       1       stream_id       Unique stream identifier for this direction (uint8)
1       1       stream_type     Type of stream (see table below) (uint8)
2       1       device_id       Source/destination device identifier (uint8)
3       2       sample_rate     Sample rate in Hz, 0 if not applicable (uint16)
5       1       sample_format   Encoding format (see table below) (uint8)
6       2       chunk_period_ms Suggested interval between StreamData frames (uint16)
```

Stream types:

```
Value   Type
0x01    Audio output (BS -> Ctrl, speaker playback)
0x02    Audio input (Ctrl -> BS, microphone capture)
0x03    Video (Ctrl -> BS, camera)
0x04-0xFF Reserved
```

Sample formats:

```
Value   Format
0x01    PCM signed 16-bit
0x02    PCM unsigned 8-bit
0x03    ADPCM (IMA)
0x10    MJPEG (video)
0x11    Raw (video, uncompressed)
0x04-0xFF Reserved / future codecs
```

**StreamData Payload:**

```
Offset  Size    Field           Description
0       1       stream_id       Stream identifier (uint8)
1       ...     data            Raw stream data
```

**StreamClose Payload:**

```
Offset  Size    Field           Description
0       1       stream_id       Stream identifier (uint8)
```

### 7.5 Telemetry and Status Messages (0x0300-0x03FF)

| Type ID | Name | Direction | Description |
|---|---|---|---|
| 0x0300 | TelemetryConfig | BS -> Ctrl | Configure periodic telemetry push |
| 0x0301 | TelemetryRequest | BS -> Ctrl | Request immediate full state dump |
| 0x0310 | ActuatorTelemetry | Ctrl -> BS | Periodic actuator state report |
| 0x0311 | SensorTelemetry | Ctrl -> BS | Periodic sensor state report |
| 0x0312 | SystemTelemetry | Ctrl -> BS | System health and diagnostics |
| 0x0320 | PostResult | Ctrl -> BS | Power-on self-test results |

**TelemetryConfig Payload:**

```
Offset  Size    Field             Description
0       2       push_interval_ms  Telemetry push interval in ms, 0 = disabled (uint16)
```

**TelemetryRequest Payload:**

Empty. The controller responds by immediately sending ActuatorTelemetry, SensorTelemetry, and SystemTelemetry.

**ActuatorTelemetry Payload:**

```
Offset  Size    Field              Description
0       4       controller_time_us Controller timestamp, µs since boot (uint32)
4       1       actuator_count     Number of actuator entries (uint8)
5       12*N    entries            Array of actuator state entries, each:
                                  actuator_id (uint8)
                                  status (uint8, see below)
                                  current_position (float32)
                                  target_position (float32)
                                  queue_depth (uint8)
                                  fault_code (uint8)
```

Actuator status values:

```
Value   Status
0x00    Idle (at target, queue empty)
0x01    Moving (executing command)
0x02    Holding (at target, active hold)
0x03    Homing (executing homing sequence)
0x04    Calibrating
0x05    Fault
0x06    Emergency stopped
```

**SensorTelemetry Payload:**

```
Offset  Size    Field              Description
0       4       controller_time_us Controller timestamp, µs since boot (uint32)
4       1       sensor_count       Number of sensor entries (uint8)
5       4*N     entries            Array of sensor state entries, each:
                                  sensor_id (uint8)
                                  status (uint8, 0x00=OK, 0x01=Fault)
                                  fault_code (uint8)
                                  reserved (uint8)
```

**SystemTelemetry Payload:**

```
Offset  Size    Field           Description
0       1       system_status   Overall system status (uint8, see below)
1       2       uptime_seconds  Time since boot (uint16, wraps)
3       1       active_streams  Number of open streams (uint8)
4       1       cpu_load        CPU utilization 0-100 (uint8)
5       1       led_state       Current LED indication pattern (uint8)
```

System status values:

```
Value   Status
0x00    Normal
0x01    Degraded (one or more faults, still operating)
0x02    Emergency stop active
0x03    Handshake incomplete
0x04    POST in progress
```

**PostResult Payload:**

```
Offset  Size    Field           Description
0       1       result_count    Number of test results (uint8)
1       3*N     results         Array of test results, each:
                                  device_id (uint8)
                                  device_type (uint8, 0x01=actuator, 0x02=sensor)
                                  result (uint8, 0x00=pass, 0x01=fail, 0x02=skipped)
```

### 7.6 Synchronization Messages (0x0020-0x002F)

| Type ID | Name | Direction | Description |
|---|---|---|---|
| 0x0020 | SyncExecute | Bidirectional | Execute all buffered messages with the given sync tag |

**SyncExecute Payload:**

```
Offset  Size    Field           Description
0       2       sync_tag        The sync tag to flush and execute (uint16)
```

SyncExecute is itself always an immediate message (sync_tag in its own header = 0x0000). It must be Ack'd.

### 7.7 Time Synchronization Messages (0x0030-0x003F)

| Type ID | Name | Direction | Description |
|---|---|---|---|
| 0x0030 | TimeSyncPulse | BS -> Ctrl | SBC reports its timestamp for a hardware sync pulse |
| 0x0031 | TimeSyncReport | Ctrl -> BS | Controller reports its input-captured timestamp |
| 0x0032 | SoftSyncRequest | BS -> Ctrl | Software clock sync request with host timestamp |
| 0x0033 | SoftSyncResponse | Ctrl -> BS | Software clock sync response with controller timestamp |

#### 7.7.1 GPIO-Based Clock Synchronization

**TimeSyncPulse Payload:**

```
Offset  Size    Field           Description
0       4       sbc_time_us     SBC timestamp when sync pulse was driven (uint32, µs)
4       1       pulse_seq       Pulse sequence counter (uint8, wraps)
```

**TimeSyncReport Payload:**

```
Offset  Size    Field              Description
0       4       ctrl_capture_us    Controller input-capture timestamp (uint32, µs since boot)
4       1       pulse_seq          Echo of pulse_seq from TimeSyncPulse (uint8)
```

**Flow:**

1. The SBC drives a rising edge on the dedicated sync GPIO line and records its own timestamp (`sbc_time_us`).
2. The controller's input capture ISR latches the hardware timer value (`ctrl_capture_us`) on the edge -- this is cycle-accurate and independent of software latency.
3. The SBC sends a TimeSyncPulse message containing `sbc_time_us` and a `pulse_seq` counter.
4. The controller matches `pulse_seq` to its captured timestamp and responds with TimeSyncReport.
5. The SBC computes: `offset = sbc_time_us - ctrl_capture_us`. This offset maps controller timestamps to SBC time.

The `pulse_seq` field disambiguates captures if the protocol message arrives late or out of order relative to the pulse. Both sides must agree on which edge triggered the capture.

TimeSyncPulse must be Ack'd. TimeSyncReport must be Ack'd. If the controller has no pending capture for the given `pulse_seq`, it Nacks with reason code 0x03 (invalid payload).

#### 7.7.2 Software Clock Synchronization

When GPIO-based clock synchronization is not available (e.g., the host is a remote development machine connected over UART/USB without a dedicated sync line), a software round-trip method provides clock offset estimation over the existing transport.

**SoftSyncRequest Payload:**

```
Offset  Size    Field           Description
0       4       host_time_us    Host timestamp at send time (uint32, µs)
4       1       sync_seq        Sequence counter for this sync exchange (uint8, wraps)
```

**SoftSyncResponse Payload:**

```
Offset  Size    Field              Description
0       4       host_time_us       Echo of host_time_us from SoftSyncRequest (uint32, µs)
4       4       ctrl_time_us       Controller timestamp at receipt of SoftSyncRequest (uint32, µs since boot)
8       1       sync_seq           Echo of sync_seq from SoftSyncRequest (uint8)
```

**Flow:**

```
Host                                   Controller
 │                                         │
 │  (records T1 = host time)               │
 │──── SoftSyncRequest { T1, seq } ──────→│
 │                                         │ (records T2 = controller time at receipt)
 │◄──── SoftSyncResponse { T1, T2, seq }──│
 │  (records T3 = host time at receipt)    │
 │                                         │
 │  rtt = T3 - T1                          │
 │  offset = T1 + rtt/2 - T2              │
 │  (maps controller timestamps to         │
 │   host time domain)                     │
```

The host computes the round-trip time and estimates the clock offset assuming symmetric latency. Multiple exchanges can be averaged to reduce jitter. The host should discard outliers (exchanges where RTT exceeds a threshold, indicating transport congestion).

Precision is bounded by transport latency jitter -- typically ~0.5-2 ms over UART at 115200 baud. This is sufficient for correlating telemetry with host-side events but not for sub-millisecond synchronization. For higher precision, use GPIO-based synchronization (Section 7.7.1).

SoftSyncRequest must be Ack'd. SoftSyncResponse must be Ack'd. The controller should process SoftSyncRequest with minimal delay between receipt and timestamping to reduce measurement error.

### 7.8 System Commands (0x0400-0x04FF)

| Type ID | Name | Direction | Description |
|---|---|---|---|
| 0x0400 | EmergencyStop | BS -> Ctrl | Halt all actuators, clear all queues |
| 0x0401 | ClearEmergencyStop | BS -> Ctrl | Resume from emergency stop |
| 0x0410 | AudioClipTransfer | BS -> Ctrl | Discrete audio clip (may be fragmented) |

**EmergencyStop Payload:**

Empty. Must be Ack'd. Controller immediately halts all actuators, clears all command queues, closes all audio output streams, and enters emergency stop state.

**ClearEmergencyStop Payload:**

Empty. Must be Ack'd. Controller exits emergency stop state and resumes accepting commands. Actuators remain in their current positions.

**AudioClipTransfer Payload:**

```
Offset  Size    Field           Description
0       1       device_id       Target audio output device (uint8)
1       1       action_mode     0x00 = Queue, 0x01 = Override (uint8)
2       1       sample_format   Encoding format (same as StreamOpen) (uint8)
3       2       sample_rate     Sample rate in Hz (uint16)
5       ...     audio_data      Raw audio samples
```

For clips exceeding the max frame size, this message uses fragmented transfer (Section 4.2). The receiver reassembles all fragments before playback (or before queuing for playback).

---

## 8. Disconnect Behavior

If either side detects communication loss (implementation-defined timeout with no valid frames received), it shall:

- **Controller**: Execute its configured disconnect stored procedure (CTRL-R25). Continue indicating status via LED.
- **Base station**: Mark the controller as disconnected and cease sending commands.

On reconnection, the full version handshake (Section 5) is repeated. After a successful handshake, the controller pushes its full state (CTRL-R26) via ActuatorTelemetry, SensorTelemetry, SystemTelemetry, and PostResult messages.

---

## 9. Timing and Flow

### 9.1 Telemetry Push

After handshake, the base station sends a TelemetryConfig to set the push interval. The controller then sends ActuatorTelemetry, SensorTelemetry, and SystemTelemetry at the configured interval. A push interval of 0 disables periodic telemetry (the base station can still use TelemetryRequest for on-demand state dumps).

### 9.2 Stream Flow Control

Streams are not flow-controlled at the protocol level. If the receiver cannot keep up with incoming stream data, it is responsible for discarding frames or closing the stream. Implementations should size buffers and chunk periods (StreamOpen.chunk_period_ms) appropriately to prevent overflow.

### 9.3 Clock Synchronization

The controller and host maintain independent clocks. To correlate controller telemetry with host-side data (e.g., video frames, command timing), the host periodically synchronizes clocks using one of two strategies. Both produce the same result: a clock offset that maps `controller_time_us` fields in telemetry to the host's time domain.

All telemetry messages (ActuatorTelemetry, SensorTelemetry) include a `controller_time_us` field stamped at the moment the telemetry is sampled. The host applies the established offset to map these timestamps to its own clock.

#### 9.3.1 GPIO-Based (High Precision)

Available when the host is an SBC with a dedicated GPIO sync line to the controller. The SBC drives a hardware pulse, and the controller latches it with a timer input capture -- eliminating software jitter on the controller side.

```
SBC                                    Controller
 │                                         │
 │──── GPIO rising edge ──────────────────→│ input capture ISR latches T_ctrl
 │     (records T_sbc)                     │
 │                                         │
 │──── TimeSyncPulse { T_sbc, seq } ──────→│
 │                                         │
 │◄──── TimeSyncReport { T_ctrl, seq } ────│
 │                                         │
 │  offset = T_sbc - T_ctrl               │
```

Precision depends on the SBC's GPIO timestamping mechanism. Even with standard Linux GPIO interrupt latency (~10-50 us), accuracy is well within a 33 ms video frame period.

See Section 7.7.1 for message definitions.

#### 9.3.2 Protocol-Based (Software Round-Trip)

Available over any transport, no GPIO required. The host sends a timestamped request, the controller echoes it with its own timestamp, and the host estimates offset from the round-trip time.

```
Host                                   Controller
 │                                         │
 │──── SoftSyncRequest { T1, seq } ──────→│ (records T2 at receipt)
 │                                         │
 │◄──── SoftSyncResponse { T1, T2, seq }──│
 │  (records T3 at receipt)                │
 │                                         │
 │  rtt = T3 - T1                          │
 │  offset = T1 + rtt/2 - T2              │
```

Precision is bounded by transport latency jitter (~0.5-2 ms over UART). Multiple exchanges should be averaged, with outliers discarded. Sufficient for telemetry correlation and UI display, but not for sub-millisecond synchronization.

See Section 7.7.2 for message definitions.

#### 9.3.3 Strategy Selection

The host selects the strategy per connection. If GPIO is available and configured, use GPIO-based sync for highest precision. Otherwise, use protocol-based sync. The host may also disable clock sync entirely if timestamps are not needed. The controller supports both strategies simultaneously -- it responds to whichever messages it receives.

Sync exchanges should repeat at a configurable interval (e.g., every 1-10 seconds) to track oscillator drift.

### 9.4 Message Ordering

Messages are processed in the order received. Within a single actuator's command queue, commands execute in FIFO order. Across different actuators, commands execute concurrently and independently. The protocol does not guarantee ordering between messages sent by different sides.

---

## 10. Reason Codes (Nack)

```
Value   Reason
0x00    Unknown / unspecified error
0x01    CRC mismatch
0x02    Unsupported message type
0x03    Invalid payload (malformed or wrong size)
0x04    Actuator ID not found
0x05    Sensor ID not found
0x06    Position out of range
0x07    System in emergency stop (commands rejected)
0x08    Stream ID not found / not open
0x09    Stream ID already in use
0x0A    Max concurrent streams exceeded
0x0B    Fragment reassembly error (timeout, duplicate, out of order)
0x0C    Protocol version mismatch
0x0D    Frame exceeds max frame size
0x0E    Device busy / queue full
0x0F    Sync tag timeout (buffered messages discarded)
0x10-0xFF Reserved
```

---

## 11. Summary of Header + Payload Structure

```
Complete message:
  [header 9B] [payload] [CRC16 2B]

Fragmented message (each fragment):
  [header 9B, flags=01] [frag_index 2B] [frag_total 2B] [fragment_data] [CRC16 2B]

Stream data:
  [header 9B, flags=10] [stream_id 1B] [stream_data] [CRC16 2B]

Synchronized message (any of the above with sync_tag != 0):
  Same structure, buffered until SyncExecute received for matching tag.
```

All wrapped in COBS encoding with 0x00 delimiter on the wire.
