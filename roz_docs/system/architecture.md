# ROZ (Robot Orchestration Zero) - System Architecture

## 1. Overview

ROZ (Robot Orchestration Zero) is a robotic home assistant platform with actuated features (head with neck, eyes, jaw), audio I/O, and a camera. The system is composed of multiple projects in a single monorepo that communicate via a custom binary protocol. This document defines the system-level architecture, project relationships, and deployment topology.

### 1.1 Purpose

This document serves as the entry point for the Roz project. It establishes the context that all per-project requirements documents reference. It does not define implementation details -- those belong to the individual project documents.

---

## 2. System Topology

### 2.1 Deployment Configurations

The system supports two deployment configurations. The host library and protocol library are identical in both. The difference is where the software stack runs, which transports are available, and whether the AI system is present.

#### (a) Direct Connection (Development)

The development machine connects to the robot's controller(s) directly over UART or USB. The host library, UI, and any ROS2 nodes run on the development machine. The AI system (roz_ai) does not run in this configuration -- it requires the SBC's GPU. This is the primary development and testing configuration.

```
                          ┌─────────────────────────────────────┐
                          │         Development Machine         │
                          │           (Ubuntu x86_64)           │
                          │                                     │
                          │  ┌───────────┐    ┌──────────────┐  │
                          │  │  roz_ui   │    │  ROS2 nodes  │  │
                          │  │ (Textual) │    │  (future)    │  │
                          │  └─────┬─────┘    └──────┬───────┘  │
                          │        │                 │          │
                          │        ▼                 ▼          │
                          │  ┌────────────────────────────────┐ │
                          │  │          roz_host              │ │
                          │  │  (host library, multi-ctrl)    │ │
                          │  └───────────────┬────────────────┘ │
                          │                  │                  │
                          │  ┌───────────────┴────────────────┐ │
                          │  │          roz_proto             │ │
                          │  │  (shared protocol library)     │ │
                          │  └───────────────┬────────────────┘ │
                          └──────────────────┼──────────────────┘
                                             │ UART / USB
                          ┌──────────────────┼──────────────────┐
                          │      Robot       │                  │
                          │                  ▼                  │
                          │  ┌────────────────────────────────┐ │
                          │  │    Embedded Controller(s)      │ │
                          │  │    (roz_firmware + roz_proto)   │ │
                          │  └──────────┬─────────────────────┘ │
                          │             │                       │
                          │         actuators, sensors,         │
                          │         speaker, mic, camera        │
                          └─────────────────────────────────────┘
```

#### (b) SBC-Hosted (Production)

A single-board computer (NVIDIA Jetson Orin Nano) is co-located in the robot. The SBC runs the AI system, host library, and ROS2 nodes. The operator connects to the SBC over the network via roz_ui. The SBC provides GPIO-based hardware clock synchronization with the controller(s).

```
  ┌───────────────────────────┐
  │     Operator Machine      │
  │                           │
  │  ┌─────────────────────┐  │
  │  │       roz_ui        │  │
  │  │     (Textual)       │  │
  │  └──────────┬──────────┘  │
  └─────────────┼─────────────┘
                │ network (roz_host Python API)
  ┌─────────────┼──────────────────────────────────────┐
  │   Robot     │                                      │
  │             ▼                                      │
  │   ┌────────────────────────────────────────────┐   │
  │   │        SBC (Jetson Orin Nano)              │   │
  │   │                                            │   │
  │   │   ┌────────────┐       ┌──────────────┐    │   │
  │   │   │   roz_ai   │       │  ROS2 nodes  │    │   │
  │   │   │  (Gemma4)  │       │  (future)    │    │   │
  │   │   └──────┬─────┘       └──────┬───────┘    │   │
  │   │          │   Python API       │            │   │
  │   │          ▼                    ▼            │   │
  │   │   ┌────────────────────────────────────┐   │   │
  │   │   │           roz_host                 │   │   │
  │   │   │    (host library, multi-ctrl)      │   │   │
  │   │   └──────────────────┬─────────────────┘   │   │
  │   │                      │                     │   │
  │   │   ┌──────────────────┴─────────────────┐   │   │
  │   │   │           roz_proto                │   │   │
  │   │   │    (shared protocol library)       │   │   │
  │   │   └──────────────────┬─────────────────┘   │   │
  │   └──────────────────────┼─────────────────────┘   │
  │                          │ UART (+ GPIO sync)      │
  │   ┌──────────────────────┼─────────────────────┐   │
  │   │                      ▼                     │   │
  │   │   ┌────────────────────────────────────┐   │   │
  │   │   │      Embedded Controller(s)        │   │   │
  │   │   │   (roz_firmware + roz_proto)       │   │   │
  │   │   └──────────────┬─────────────────────┘   │   │
  │   │                  │                         │   │
  │   │          actuators, sensors,               │   │
  │   │          speaker, mic, camera              │   │
  │   └────────────────────────────────────────────┘   │
  └────────────────────────────────────────────────────┘
```

---

## 3. Project Inventory

### 3.1 roz_proto -- Shared Protocol Library

**Language:** C (C99, no extensions)
**Builds to:** Static library (.a), also compilable as source inclusion for embedded targets
**OS dependencies:** None. Portable to bare metal and Linux.

Implements the binary wire protocol defined in [wire_protocol.md](../protocol/wire_protocol.md):
- COBS encoding/decoding
- CRC-16/CCITT computation
- Frame accumulation, validation, and construction
- Message serialization and deserialization for all message types
- Handshake state machine
- Sequence number management
- Protocol type definitions (structs, enums, constants)

Both the embedded controller firmware and the host library link against this library. This ensures protocol consistency -- there is one implementation of each message parser, not two.

All functions are re-entrant. State is passed via caller-owned context structs, not static globals. This supports multiple simultaneous connections on the host side and keeps the embedded side clean.

**Requirements:** [proto_lib/requirements.md](../proto_lib/requirements.md)

### 3.2 roz_firmware -- Embedded Controller Firmware

**Language:** C (bare metal, Cortex-M0+)
**Target:** NUCLEO-L031K6 (STM32L031K6Tx, 32KB Flash, 8KB RAM)
**Toolchain:** arm-none-eabi-gcc, STM32CubeIDE

Firmware for a single embedded controller responsible for actuator control, interpolation, sensor monitoring, and real-time device management. Communicates with the host over UART/SPI/USB using the shared protocol library.

Each controller manages a set of actuators and sensors defined at compile time. A robot may have multiple controllers (e.g., one for the head, one for each arm), each running this firmware with a different device configuration.

**Requirements:** [controller/requirements.md](../controller/requirements.md)
**Module Design:** [controller/module_design.md](../controller/module_design.md)

### 3.3 roz_host -- Base Station Host Library

**Language:** C (C99, POSIX)
**Builds to:** Shared library (.so) for Python, static library (.a) for C consumers
**OS dependencies:** Linux (POSIX threads, termios for UART)

The host-side counterpart to the embedded controller. Manages one or more controller connections, provides a thread-safe command API, dispatches telemetry via callbacks, and exposes a C API designed for Python wrapping.

Links against roz_proto for all protocol operations. Adds:
- Linux transport backends (UART, USB, future TCP)
- Background receive thread per connection
- Ack/nack tracking with timeouts
- Multi-controller connection management
- Clock synchronization (protocol-based and GPIO-based implementations)
- Python bindings (cffi or ctypes)

**Requirements:** [host_lib/requirements.md](../host_lib/requirements.md)

### 3.4 roz_ai -- AI System

**Language:** Python
**Target:** NVIDIA Jetson Orin Nano (8GB, 40 TOPS)
**Key dependencies:** Gemma4 (multimodal LLM), TTS model, roz_host Python API

The robot's cognitive core. Runs a continuous perception-reasoning-action loop: ingests raw audio, downsampled camera frames, and controller telemetry; reasons about the environment using a multimodal LLM (Gemma4); and produces actions (speech, motion, expressions) executed through roz_host.

The system is always on -- no wake word or activation trigger. It maintains continuous audio and visual awareness, manages conversational state, and generates idle behaviors when not actively engaged.

Key components:
- Perception pipeline (audio, vision, telemetry preprocessing)
- Core reasoning (Gemma4 multimodal LLM inference)
- Action generation (motor skills via control policies, jaw synchronization)
- TTS pipeline (on-device text-to-speech)
- Behavior manager (personality, conversation state, attention)
- Safety layer (content filtering, command validation)

**Requirements:** [ai/requirements.md](../ai/requirements.md)

### 3.5 roz_ui -- Operator Interface (Future)

**Language:** Python
**Framework:** Textual (terminal UI)

Terminal-based UI for robot operation. Displays telemetry, allows manual actuator control, shows system status, and provides AI system debug visibility. Communicates with controllers through roz_host's Python API.

**Requirements:** [ui/requirements.md](../ui/requirements.md)

---

## 4. Dependency Graph

```
  roz_ai (Python, SBC)       roz_ui (Python)
      │                          │
      │ Python API               │ Python API
      ├──────────────┬───────────┘
      │              │
      ▼              ▼
  roz_host (C, Linux)
      │
      │ C API
      ▼
  roz_proto (C, portable)
      │                  │
      │ source/link      │ source/link
      ▼                  ▼
  roz_host internals   roz_firmware
                       (embedded firmware)
```

Dependencies flow strictly downward. roz_proto has no upward dependencies -- it is a pure library. roz_host depends on roz_proto but not on any specific controller firmware. roz_ai and roz_ui both consume roz_host's Python API independently -- roz_ai drives the robot's behavior, roz_ui provides operator visibility and manual control.

---

## 5. Multi-Controller Architecture

A robot may have multiple embedded controllers, each managing a disjoint set of actuators and sensors. Examples:

| Controller | Function | Actuators |
|---|---|---|
| Head controller | Neck, eyes, jaw, speaker, mic, camera | 4 servos + peripherals |
| Arm controller (future) | Shoulder, elbow, wrist, gripper | N servos/steppers |
| Leg controller (future) | Hip, knee, ankle | N servos/steppers |

### 5.1 Identification

Each controller has a unique identifier within the robot. This identifier is established during the version handshake -- the handshake payload should be extended to include a controller ID field so the host can distinguish controllers.

### 5.2 Host-Side Management

The host library manages each controller as an independent connection with its own:
- Transport instance
- Handshake state
- Sequence number counter
- Telemetry callback set
- Ack tracking state

The host API addresses commands to a specific controller handle. The host library does not perform cross-controller coordination -- that is the responsibility of the application layer (UI, ROS2 node) which can issue synchronized commands to multiple controllers.

### 5.3 Shared Protocol

All controllers speak the same wire protocol (roz_proto). A controller's device registry defines which actuator IDs and sensor IDs it owns. Actuator IDs are scoped per-controller -- actuator 0 on the head controller is unrelated to actuator 0 on an arm controller. The host library's controller handle disambiguates.

---

## 6. Clock Synchronization

Two clock synchronization strategies are supported, selectable at connection time:

### 6.1 GPIO-Based (High Precision)

Available when the host is an SBC with GPIO access to the controller. The SBC drives a hardware sync pulse, and the controller latches it with a timer input capture. Achieves microsecond-level accuracy.

This is the mechanism defined in [wire_protocol.md](../protocol/wire_protocol.md) Section 7.7 and controller requirement CTRL-R30. Requires:
- Physical GPIO connection between SBC and controller
- Linux GPIO access on the host (e.g., libgpiod)
- Timer input capture configured on the controller

### 6.2 Protocol-Based (Software Round-Trip)

Available over any transport, no GPIO required. Uses a request-response timestamp exchange to estimate clock offset from round-trip time. Less precise (bounded by transport latency jitter, typically ~1ms over UART) but sufficient for many use cases and always available.

This requires a new message pair to be added to the wire protocol:
- **SoftSyncRequest** (Host -> Controller): host timestamp at send time
- **SoftSyncResponse** (Controller -> Host): echo of host timestamp + controller timestamp at receipt

The host computes offset using the standard NTP-style half-round-trip estimation. Multiple exchanges can be averaged to reduce jitter.

### 6.3 Strategy Selection

The host library selects the clock sync strategy per connection:
- If GPIO is available and configured, use GPIO-based sync.
- Otherwise, fall back to protocol-based sync.
- The application may also disable clock sync entirely if not needed.

Both strategies produce the same output: a clock offset that maps controller timestamps to host time. Consumers of telemetry data do not need to know which strategy is in use.

---

## 7. AI System

The AI system (roz_ai) is the robot's cognitive core. It runs on the SBC (Jetson Orin Nano) and operates a continuous perception-reasoning-action loop for the full duration the robot is powered on.

```
  ┌───────────────────────────────────────────────────────────┐
  │                     roz_ai (SBC)                          │
  │                                                           │
  │  audio, video,    ┌──────────────┐    actuator commands,  │
  │  telemetry ──────▶│  Multimodal  │──────▶ speech audio,   │
  │  (via roz_host)   │  LLM (Gemma4)│      expressions      │
  │                   └──────┬───────┘      (via roz_host)    │
  │                          │                                │
  │          ┌───────────────┼───────────────┐                │
  │          ▼               ▼               ▼                │
  │    ┌──────────┐   ┌───────────┐   ┌───────────┐          │
  │    │ Behavior │   │    TTS    │   │   Motor   │          │
  │    │ Manager  │   │ Pipeline  │   │  Skills   │          │
  │    └──────────┘   └───────────┘   └───────────┘          │
  └───────────────────────────────────────────────────────────┘
```

The perception pipeline fuses audio, camera frames, and controller telemetry into a multimodal context for each inference cycle. The LLM produces structured output that the action generation layer parses into motor commands, speech, and expressions, all executed through roz_host's Python API.

The system has no wake word or activation trigger -- it maintains continuous sensory awareness, manages conversational state, and generates idle behaviors when not actively engaged. A behavior manager governs personality, attention, and turn-taking. A safety layer filters LLM output before it reaches the actuators.

All GPU-intensive workloads (LLM inference, TTS) run on the Jetson's GPU. The system manages shared GPU scheduling between these pipelines.

**Requirements:** [ai/requirements.md](../ai/requirements.md)

---

## 8. ROS2 Integration

ROS2 is a system-level concern, not owned by any single project. The integration path:

```
  ROS2 Topics/Services
         │
  ┌──────▼──────┐
  │  ROS2 Node  │  (C++ or Python node, future project)
  │  (driver)   │
  └──────┬──────┘
         │ roz_host API
         ▼
     roz_host
```

A ROS2 driver node will use roz_host's API (either the C API from a C++ node, or the Python API from a Python node) to bridge between ROS2 topics/services and the robot's controllers. The driver node is a thin translation layer -- roz_host owns the connection lifecycle, protocol handling, and telemetry dispatch.

The wire protocol (roz_proto) is designed so that its message types map naturally to ROS2 message/service patterns:
- Actuator commands -> ROS2 action or service calls
- Telemetry -> ROS2 topic publications
- Emergency stop -> ROS2 service
- Streams -> ROS2 topics with QoS for real-time data

The ROS2 driver node is out of scope for now. The design of roz_host should facilitate it but not depend on ROS2.

---

## 9. Documentation Map

All project documentation lives in this repository (`roz_docs/`):

| Path | Contents |
|---|---|
| `system/architecture.md` | This document. System context, project relationships. |
| `protocol/wire_protocol.md` | Binary wire protocol specification. |
| `controller/requirements.md` | Embedded controller firmware requirements. |
| `controller/module_design.md` | Embedded controller module and type design. |
| `proto_lib/requirements.md` | Shared protocol library requirements. |
| `host_lib/requirements.md` | Base station host library requirements. |
| `ai/requirements.md` | AI system requirements. |
| `ui/requirements.md` | Operator UI requirements. |

Individual project repositories contain only code, build configuration, and a project-level CLAUDE.md for tooling guidance. They do not contain requirements or design documents.

---

## 10. Design Notes

### 10.1 Why a Shared Protocol Library

Earlier designs had the controller firmware and base station each implementing their own protocol parsing. This creates a maintenance burden and a class of bugs where the two sides disagree on message layout, field order, or enum values. A single shared implementation eliminates this. The cost -- building a portable C library that works on both Cortex-M0+ and Linux -- is low, since the protocol layer is already pure C with no OS dependencies.

### 10.2 Why C

The embedded controller requires C (bare metal Cortex-M0+, 8KB RAM). The shared protocol library must therefore be C. The host library is also C to provide the cleanest possible Python FFI surface (no C++ name mangling, no Rust toolchain dependency). The UI is Python, using the host library via cffi or ctypes.

### 10.3 Multi-Controller vs. Multi-Robot

The system is designed for one robot with multiple controllers, not multiple robots. Each controller manages a disjoint set of actuators/sensors on the same physical robot. Cross-robot coordination (if ever needed) would be handled at the ROS2 layer, not in roz_host.

### 10.4 Naming

"Roz" is the robot's name. Project prefixes use `roz_` for consistency. The wire protocol and shared library use `roz_proto` to distinguish from the higher-level host library.
