# ROZ Shared Protocol Library (roz_proto) - Requirements Document

## 1. Overview

This document defines the requirements for roz_proto, a shared C library that implements the ROZ binary wire protocol defined in [wire_protocol.md](../protocol/wire_protocol.md). The library is linked by both the embedded controller firmware (roz_firmware) and the base station host library (roz_host), ensuring a single authoritative implementation of the protocol.

### 1.1 Scope

roz_proto covers the protocol layer and below: framing, encoding, integrity checking, message serialization/deserialization, handshake logic, and protocol type definitions. It does **not** cover transport I/O, threading, timeout management, application-level policies (retry, reconnect), or any OS-specific functionality.

The library is a codec and state machine library. It transforms between typed C structs and wire bytes, and manages protocol state machines (handshake, sequence numbers). All I/O is the caller's responsibility.

### 1.2 System Context

```
┌───────────────────────────────────────────────────┐
│            Caller (roz_host or roz_firmware)       │
│                                                   │
│  ┌─ raw bytes from transport ──┐  ┌─ messages ──┐ │
│  │                             │  │ to send     │ │
│  │         ┌───────────────────┴──┴───┐         │ │
│  │         │       roz_proto          │         │ │
│  │         │                          │         │ │
│  │         │  framing ←→ protocol     │         │ │
│  │         │  COBS, CRC   serialize   │         │ │
│  │         │  accumulate  deserialize │         │ │
│  │         │              handshake   │         │ │
│  │         └───────────┬──────────────┘         │ │
│  │                     │                        │ │
│  │         encoded frames ready to send         │ │
│  │         parsed messages ready to process     │ │
│  └─────────────────────┴────────────────────────┘ │
│                                                   │
│  Caller handles: transport I/O, threading,        │
│  timeouts, application logic                      │
└───────────────────────────────────────────────────┘
```

### 1.3 Reference Documents

- [Wire Protocol Specification](../protocol/wire_protocol.md) -- the normative protocol definition. roz_proto implements this specification.
- [System Architecture](../system/architecture.md) -- system context and project relationships.
- [Controller Module Design](../controller/module_design.md) -- the existing embedded design, from which roz_proto extracts the shared protocol layers.

---

## 2. Requirements

### 2.1 Portability and Constraints

**PROTO-R1 - Language Standard**: The library shall be written in C99 with no compiler-specific extensions. All source files shall compile cleanly under both `arm-none-eabi-gcc` (bare metal) and a standard Linux `gcc`/`clang`.

**PROTO-R2 - No Operating System Dependencies**: The library shall not depend on any operating system, runtime library beyond the C99 standard library, or hardware-specific headers. It shall be compilable and functional on bare metal targets and Linux hosts.

**PROTO-R3 - No Dynamic Memory Allocation**: The library shall not call `malloc`, `calloc`, `realloc`, `free`, or any other dynamic memory allocator. All state and buffer storage shall be provided by the caller via context structs or shall be compile-time configurable static allocations within caller-owned structs.

**PROTO-R4 - Re-entrancy**: All library functions shall be re-entrant. The library shall not use file-scoped static mutable variables. All mutable state shall reside in caller-owned context structs passed by pointer. This enables the host library to manage multiple simultaneous controller connections, each with independent protocol state.

**PROTO-R5 - Configurable Buffer Sizes**: Buffer sizes (maximum frame size, COBS accumulation buffer, etc.) shall be configurable via compile-time defines with sensible defaults. This allows the embedded target to use minimal buffers while the host target uses larger ones.

**PROTO-R6 - No I/O**: The library shall not perform any I/O operations (no reads, writes, file access, or hardware access). The caller provides raw bytes to the library and receives encoded bytes from it. The library never initiates communication.

### 2.2 Encoding

**PROTO-R7 - COBS Encoding**: The library shall provide functions to encode arbitrary byte sequences using Consistent Overhead Byte Stuffing (COBS), as defined in [wire_protocol.md](../protocol/wire_protocol.md) Section 2.1.
  - (a) The encoder shall eliminate all 0x00 bytes from the output.
  - (b) The encoder shall report the encoded length.
  - (c) The encoder shall operate on caller-provided input and output buffers with no internal buffering.

**PROTO-R8 - COBS Decoding**: The library shall provide functions to decode COBS-encoded byte sequences back to the original data.
  - (a) The decoder shall report the decoded length.
  - (b) The decoder shall detect and report malformed COBS sequences (e.g., invalid block length).
  - (c) The decoder shall operate on caller-provided input and output buffers with no internal buffering.

**PROTO-R9 - CRC-16/CCITT**: The library shall provide a function to compute CRC-16/CCITT checksums (polynomial 0x1021, initial value 0xFFFF) over arbitrary byte sequences, as defined in [wire_protocol.md](../protocol/wire_protocol.md) Section 2.3.

### 2.3 Framing

**PROTO-R10 - Frame Accumulation**: The library shall provide a stateful frame accumulator that accepts raw bytes from the caller (as received from a transport) and detects frame boundaries.
  - (a) The accumulator shall buffer incoming bytes until a 0x00 delimiter is received.
  - (b) Upon receiving a delimiter, the accumulator shall COBS-decode the buffered data and validate the CRC-16.
  - (c) The accumulator shall report whether a complete, valid frame is available after each call.
  - (d) The accumulator shall handle back-to-back frames, partial frames across multiple calls, and multiple frames within a single byte buffer.
  - (e) If the accumulation buffer overflows before a delimiter is received, the accumulator shall discard the partial frame and report an error.

**PROTO-R11 - Frame Construction**: The library shall provide functions to construct outgoing frames from a header and payload.
  - (a) The constructor shall compute and append the CRC-16 over the header and payload.
  - (b) The constructor shall COBS-encode the raw frame (header + payload + CRC).
  - (c) The constructor shall append the 0x00 delimiter to the encoded output.
  - (d) The constructor shall report the total encoded length for the caller to transmit.
  - (e) The constructor shall reject frames that exceed the configured maximum frame size.

**PROTO-R12 - Frame Validation**: Upon receiving a complete frame, the library shall:
  - (a) Verify that the frame meets the minimum size (header + CRC = 11 bytes).
  - (b) Verify the CRC-16 matches.
  - (c) Verify that the `payload_length` field in the header is consistent with the actual frame size.
  - (d) Report validation failures with a specific error code (CRC mismatch, size mismatch, truncated frame).

### 2.4 Header Parsing

**PROTO-R13 - Header Serialization**: The library shall provide functions to serialize a frame header struct into a 9-byte wire representation and to deserialize a 9-byte wire representation into a frame header struct, as defined in [wire_protocol.md](../protocol/wire_protocol.md) Section 3.
  - (a) All multi-byte fields shall be serialized in little-endian byte order.
  - (b) The serializer shall extract the transfer type from the flags field.

### 2.5 Message Serialization

**PROTO-R14 - Message Serialize/Deserialize**: The library shall provide a matched pair of serialize and deserialize functions for every message type defined in [wire_protocol.md](../protocol/wire_protocol.md) Section 7, including:
  - (a) Protocol control: VersionRequest, VersionConfirm, Ack, Nack, SyncExecute.
  - (b) Time synchronization: TimeSyncPulse, TimeSyncReport, SoftSyncRequest, SoftSyncResponse.
  - (c) Actuator commands: ActuatorCommand, CoordinatedCommand.
  - (d) Stream control: StreamOpen, StreamData, StreamClose.
  - (e) Telemetry: TelemetryConfig, TelemetryRequest, ActuatorTelemetry, SensorTelemetry, SystemTelemetry, PostResult.
  - (f) System commands: EmergencyStop, ClearEmergencyStop, AudioClipTransfer.

**PROTO-R15 - Serialization Correctness**: Each serialize function shall produce a byte sequence that, when passed to the corresponding deserialize function, yields an identical struct. Each deserialize function shall:
  - (a) Validate that the payload length is correct for the message type (fixed-size messages) or meets the minimum size (variable-size messages).
  - (b) Return an error code if the payload is malformed (wrong size, invalid field values).
  - (c) Handle variable-length payloads (CoordinatedCommand, ActuatorTelemetry, SensorTelemetry, PostResult) by reading the count field and validating the remaining length against it.

**PROTO-R16 - Byte Order**: All multi-byte fields shall be serialized in little-endian byte order as specified in [wire_protocol.md](../protocol/wire_protocol.md) Section 2.4. The serialize/deserialize functions shall handle byte order conversion regardless of the host platform's native endianness.

### 2.6 Protocol Type Definitions

**PROTO-R17 - Canonical Type Definitions**: The library shall define the canonical C types for all protocol entities, serving as the single source of truth for both the embedded firmware and the host library. This includes:
  - (a) Identifier typedefs: `actuator_id_t`, `sensor_id_t`, `device_id_t`, `stream_id_t`, `seq_num_t`, `msg_type_t`, `sync_tag_t`, `controller_id_t`.
  - (b) Frame header struct.
  - (c) A message payload struct for each message type.
  - (d) Enumerations: nack reason codes, actuator status values, system status values, stream types, sample formats, action modes, transfer types.
  - (e) Message type ID constants for all message types.
  - (f) Flags field constants and extraction macros.

**PROTO-R18 - Type Stability**: Protocol type definitions shall be organized in dedicated headers separate from implementation code. Consumers (roz_firmware, roz_host) include these headers directly. Changes to type definitions constitute a protocol change and shall be coordinated across all consumers.

### 2.7 Fragmentation

**PROTO-R19 - Fragment Construction**: The library shall provide functions to split an oversized message into a sequence of fragment frames, as defined in [wire_protocol.md](../protocol/wire_protocol.md) Section 4.2.
  - (a) Each fragment shall include the fragmentation header (fragment_index, fragment_total).
  - (b) Each fragment shall respect the configured maximum frame size.
  - (c) Each fragment shall be assigned a unique sequence number (via the sequence number manager).
  - (d) The library shall report the number of fragments produced.

**PROTO-R20 - Fragment Reassembly**: The library shall provide a stateful fragment reassembler that accepts individual fragment frames and produces the complete reassembled payload.
  - (a) The reassembler shall track received fragments by index and detect duplicates.
  - (b) The reassembler shall report when all fragments have been received and the payload is complete.
  - (c) The reassembler shall detect and report errors: missing fragments, index out of range, inconsistent fragment_total across fragments of the same message.
  - (d) The reassembler shall not enforce timeouts (it has no concept of time). The caller is responsible for detecting stalled reassembly and resetting the reassembler.
  - (e) Reassembly buffer storage shall be provided by the caller.

### 2.8 Sequence Number Management

**PROTO-R21 - Sequence Counter**: The library shall provide a sequence number manager that:
  - (a) Maintains a monotonically increasing uint16 counter per context.
  - (b) Wraps from 0xFFFF to 0x0000.
  - (c) Returns the next sequence number and advances the counter on each call.
  - (d) Is independent per protocol context (each connection has its own counter).

### 2.9 Handshake State Machine

**PROTO-R22 - Handshake Logic**: The library shall implement the version handshake state machine defined in [wire_protocol.md](../protocol/wire_protocol.md) Section 5.
  - (a) The state machine shall support the states: idle, waiting for remote VersionRequest, waiting for remote VersionConfirm, complete, and failed.
  - (b) On receiving a remote VersionRequest, the state machine shall compute the highest common protocol version from the intersection of both sides' version lists.
  - (c) If no common version exists, the state machine shall transition to the failed state and produce a VersionConfirm with `selected_version = 0x00`.
  - (d) The state machine shall compute the effective frame size as the minimum of both sides' `max_frame_size`.
  - (e) On receiving a remote VersionConfirm, the state machine shall verify that the selected version and effective frame size match its own computation.
  - (f) The state machine shall handle the controller_id field as defined in [wire_protocol.md](../protocol/wire_protocol.md) Section 5.5.

**PROTO-R23 - Handshake Output**: The handshake state machine shall produce outgoing messages (VersionRequest, VersionConfirm) as serialized byte buffers for the caller to transmit. It shall not transmit them itself.

**PROTO-R24 - Handshake Result**: Upon completion (success or failure), the handshake state machine shall expose the result to the caller:
  - (a) Selected protocol version (or 0x00 on failure).
  - (b) Effective frame size.
  - (c) Remote controller ID.
  - (d) Current state (for the caller to distinguish success from failure).

### 2.10 Message Dispatch

**PROTO-R25 - Message Routing**: The library shall provide a dispatch function that, given a validated frame, parses the header and routes the message to a caller-registered handler based on the message type.
  - (a) The caller shall be able to register handler functions for individual message types or message type ranges.
  - (b) If no handler is registered for a received message type, the dispatcher shall invoke a default handler (if registered) or return an error code.
  - (c) The dispatcher shall parse the header and present the handler with a parsed header struct and a pointer to the raw payload bytes.

**PROTO-R26 - Sync Tag Handling**: The dispatch function shall inspect the `sync_tag` field of incoming messages.
  - (a) If `sync_tag` is 0x0000, the message shall be dispatched to the appropriate handler immediately.
  - (b) If `sync_tag` is non-zero, the message shall be forwarded to a caller-provided sync buffer callback instead of the normal handler. The caller (roz_host or roz_firmware) owns sync group buffering and execution policy.

### 2.11 Convenience Builders

**PROTO-R27 - Ack/Nack Construction**: The library shall provide convenience functions to construct Ack and Nack messages given a reference sequence number (and reason code for Nack), producing a complete frame ready for transmission.

**PROTO-R28 - Common Message Builders**: The library shall provide convenience functions to construct commonly sent messages (ActuatorCommand, TelemetryConfig, EmergencyStop, SyncExecute, SoftSyncRequest, etc.) from typed parameters, producing a complete frame ready for transmission. Each builder shall:
  - (a) Serialize the message payload.
  - (b) Build the header with the correct message type, a sequence number from the context's sequence counter, the specified sync_tag (defaulting to 0x0000), and the appropriate transfer type flag.
  - (c) Construct the complete frame (header + payload + CRC, COBS-encoded, with delimiter).
  - (d) Return the encoded frame in a caller-provided output buffer.

### 2.12 Error Reporting

**PROTO-R29 - Error Codes**: All library functions that can fail shall return a status code from a defined enumeration. The enumeration shall distinguish between:
  - (a) Success.
  - (b) Buffer too small (output buffer cannot hold the result).
  - (c) Invalid input (malformed data, null pointer, invalid parameters).
  - (d) CRC mismatch.
  - (e) COBS decode error.
  - (f) Payload size mismatch.
  - (g) Handshake failure (no common version, confirm mismatch).
  - (h) Fragment error (duplicate, out of range, inconsistent total).
  - (i) No handler registered (dispatch).

**PROTO-R30 - No Side Effects on Error**: If a function fails, it shall not modify the output buffer or context state. The caller can safely retry or take corrective action without needing to reinitialize.

---

## 3. Design Notes

### 3.1 Relationship to Existing Controller Code

The existing controller module design ([module_design.md](../controller/module_design.md)) defines a framing layer (Sections 5.1-5.3) and protocol layer (Section 6) that are conceptually identical to what roz_proto provides. When roz_proto is implemented, the controller firmware will replace its local framing and protocol code with roz_proto, while keeping its application-level modules (actuator manager, stream manager, etc.) unchanged.

### 3.2 What Stays in the Caller

The following responsibilities remain with the caller (roz_host or roz_firmware), not roz_proto:

- **Transport I/O**: Reading bytes from UART/USB/SPI, writing encoded frames to the transport.
- **Timeout management**: Detecting stalled fragment reassembly, handshake timeout, disconnect detection. The library has no concept of time.
- **Sync group buffering and execution**: The library identifies sync-tagged messages and forwards them to the caller. The caller decides how to buffer and when to execute.
- **Retry policy**: The library constructs Nack messages but does not implement retry logic.
- **Application-level message handling**: The library dispatches parsed messages to handlers. The handlers themselves (actuator control, telemetry gathering, stream management) are the caller's responsibility.

### 3.3 Buffer Ownership

The library follows a consistent pattern: the caller provides all buffers. For frame accumulation, the caller allocates the framing context struct (which contains the accumulation buffer). For frame construction, the caller provides the output buffer. For fragment reassembly, the caller provides the reassembly buffer. This avoids hidden memory allocation and gives the caller full control over memory layout -- critical for the embedded target's memory budget.

### 3.4 Compile-Time Configuration

Buffer sizes and limits are controlled by preprocessor defines, with defaults suitable for the embedded target:

```
ROZ_MAX_FRAME_SIZE      -- max raw frame size (default: 256)
ROZ_COBS_BUF_SIZE       -- COBS accumulation buffer (default: ROZ_MAX_FRAME_SIZE + overhead)
ROZ_MAX_HANDLERS        -- max registered message handlers (default: 16)
```

The host library overrides these with larger values at compile time.

### 3.5 Endianness

The wire protocol is little-endian. The serialize/deserialize functions handle byte order conversion explicitly (byte-by-byte packing/unpacking) rather than relying on host endianness or compiler struct packing. This ensures correctness on both little-endian (ARM Cortex-M, x86) and big-endian targets, without requiring `__attribute__((packed))` or similar extensions.

### 3.6 Testing Strategy

Because roz_proto has no OS or hardware dependencies, it can be compiled and tested natively on the development machine. Round-trip tests (serialize → frame → COBS encode → COBS decode → deframe → deserialize → compare) provide high coverage of protocol correctness. Fuzz testing of the decoder and deserializer paths is recommended to verify robustness against malformed input.
