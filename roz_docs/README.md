# ROZ (Robot Orchestration Zero) - Documentation

ROZ is a robotic home assistant platform with actuated features, audio I/O, and a camera. The system is composed of multiple projects -- embedded controllers, a shared protocol library, a C/C++ motor control runtime, a Python server daemon, a compound AI system, and an operator interface -- that communicate via a custom binary protocol.

This documentation covers the full system design, wire protocol specification, and per-project requirements.

---

## System

- [Architecture](system/architecture.md) -- System topology, project inventory, deployment configurations, multi-controller design, clock synchronization strategy, and ROS2 integration path.

## Protocol

- [Wire Protocol](protocol/wire_protocol.md) -- Binary communication protocol specification. Defines framing (COBS, CRC-16), message types, transfer modes (complete, fragmented, stream), version handshake, synchronized execution, time synchronization, and all message payloads.

## Embedded Controller (roz_firmware)

- [Requirements](controller/requirements.md) -- Firmware requirements for the embedded actuator controller. Hardware abstraction, actuator control, audio/video, communication, safety, and startup behavior.
- [Module Design](controller/module_design.md) -- Module architecture, type definitions, data flow, and memory budget for the embedded controller firmware.

## Shared Protocol Library (roz_proto)

- [Requirements](proto_lib/requirements.md) -- Portable C library implementing the wire protocol. COBS, CRC, framing, message serialization, handshake logic. Shared between embedded firmware and host library.

## Motor Control Runtime (roz_control)

- [Requirements](control/requirements.md) -- *(planned)* C/C++ motor control runtime. 1000Hz control policy loop, driver module (transport backends, connection management), multi-controller support, ack tracking, clock synchronization. Pure C API, no Python code.

## Server Daemon (roz_server)

- Requirements -- *(planned)* Python server daemon running on the SBC. Hosts roz_ai, loads roz_control via C ABI, serves roz_ui over network. Supports AI and manual control modes.

## AI System (roz_ai)

- [Requirements](ai/requirements.md) -- Compound AI system with multimodal LLM (Gemma4) at the core. Continuous perception loop (audio, vision, telemetry), action generation, TTS, behavior management, and safety. Runs on the SBC.

## Operator Interface (roz_ui)

- [Requirements](ui/requirements.md) -- Terminal-based operator UI (Textual). Multi-robot dashboard, hierarchical drill-down views (robots, controllers, actuators, sensors), real-time pushed telemetry, k9s-style navigation.
