# Embedded Robot Controller

General-purpose embedded controller firmware for robotic systems. Manages actuators, sensors, and media peripherals over a transport-agnostic binary protocol, with a companion computer or base station handling high-level coordination and ROS2 integration.

The firmware is hardware-independent by design. All actuator types (servos, steppers, linear actuators, binary outputs), sensors, and communication transports are accessed through abstract interfaces. A compile-time device registry defines the specific robot configuration -- swapping hardware means changing a config table and a PAL implementation, not the application logic.

## Key capabilities

- **Actuator control with interpolation** -- queue/override command semantics, coordinated multi-actuator motion, configurable homing and hold behaviors
- **Audio and video streaming** -- bidirectional streams with queue/override semantics matching actuator control
- **Synchronized execution** -- sync tags allow atomic execution of grouped commands across actuators and streams (e.g., coordinating motion with audio)
- **Telemetry and diagnostics** -- configurable periodic state reporting, power-on self-test, continuous built-in test, emergency stop
- **Hardware clock synchronization** -- cycle-accurate input capture for correlating controller timestamps with companion computer time
- **Bare-metal, no dynamic allocation** -- cooperative main loop, all state statically allocated, targeting resource-constrained MCUs (reference: Cortex-M0+, 32KB flash, 8KB RAM)

## Documentation

Project documentation lives in the central [roz_docs](../roz_docs/index.md) directory:

- [Requirements](../roz_docs/controller/requirements.md) -- system requirements, hardware abstraction model, actuator/sensor models, safety, and communication requirements
- [Communication Protocol](../roz_docs/protocol/wire_protocol.md) -- binary wire protocol specification: COBS framing, message catalog, stream management, version handshake, and clock synchronization
- [Module Design](../roz_docs/controller/module_design.md) -- firmware architecture, module interfaces, data types, data flow, memory budget, and file organization
