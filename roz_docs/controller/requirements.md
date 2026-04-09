# ROZ Embedded Controller (roz_firmware) - Requirements Document

## 1. Overview

This document defines the requirements for the embedded controller firmware. The controller manages actuators, sensors, and peripherals on a single subsystem of the robot (e.g., head, arm, leg). It receives commands from and transmits sensor data to a host via the shared wire protocol.

### 1.1 Scope

This project covers the **embedded controller firmware only**. The controller communicates with a base station over a transport-agnostic protocol. ROS2 integration is a system-level requirement fulfilled by the base station, which will implement a ROS2 driver node that bridges between ROS2 topics and the controller's wire protocol. The controller firmware itself does not depend on or implement ROS2.

### 1.2 System Context

```
+-----------------+        ROS2/Zenoh        +-------------------+
|                 | <---------------------> |                   |
|  Base Station   |                         | Companion Computer|
|  (ROS2)         |                         | (e.g., Jetson)    |
|                 |                         |                   |
+-----------------+                         +-------------------+
                                              | custom protocol
                                              | (UART/SPI/USB)
                                            +-------------------+
                                            |    Embedded       |
                                            |    Controller     |
                                            |  (this project)   |
                                            +-------------------+
                                              |  |  |  |  |  |
                                           actuators, sensors,
                                           speaker, mic, camera
```

The companion computer (likely an NVIDIA Jetson) handles media streaming (camera, microphone, speaker), ROS2 communication, and high-level coordination. It may also serve as the base station itself. The embedded controller owns actuator control, interpolation, and real-time device management, communicating with the companion via a custom binary protocol over an abstracted transport layer.

### 1.3 Reference Hardware

The current prototype targets the NUCLEO-L031K6 (STM32L031K6Tx, Cortex-M0+, 32KB Flash, 8KB RAM). The current actuator configuration is:

| Actuator | Function | Current HW |
|---|---|---|
| Neck yaw | Head left/right rotation | Servo on TIM2 CH1 (PA0) |
| Eye horizontal | Left/right eye direction | Servo on TIM2 CH2 (PA1) |
| Eye vertical | Up/down eye direction | Servo on TIM2 CH3 (PB0) |
| Jaw | Mouth open/close | Servo on TIM22 CH2 (PA7) |

Additional peripherals: speaker (audio output), microphone (audio input), camera (in eye).

This hardware configuration is for reference only. The firmware design shall not assume this specific set of actuators, sensors, or peripherals.

---

## 2. Requirements

### 2.1 Hardware Abstraction

**R1 - Hardware Independence**: The controller firmware shall not be coupled to any specific processor, motor type, communication interface, or peripheral hardware. All hardware interactions shall be mediated through abstract interfaces.

**R2 - Actuator Model**: Each actuator shall be described by:
  - (a) A unique identifier.
  - (b) A type classification (e.g., rotational servo, linear actuator, continuous rotation, binary on/off).
  - (c) A configuration defining its range of motion, mechanical limits, and default position.
  - (d) A homing behavior specification (see R17).
  - (e) A hold behavior specification defining whether the actuator actively holds position or goes passive when idle (see R24).

**R3 - Sensor Model**: Each sensor shall be described by:
  - (a) A unique identifier.
  - (b) A type classification (e.g., audio input, video input, position feedback, temperature).
  - (c) A configuration defining its data format, sample rate, and streaming parameters.

**R4 - Device Registry**: The controller shall maintain a registry of all actuators and sensors. The device configuration is assumed to be known at compile time. The design should allow for future extension toward runtime self-description, enabling a robot to advertise its capabilities to the base station at connection time.

### 2.2 Actuator Control

**R5 - Actuator Control API**: The controller shall expose a high-level API for actuator control that accepts commands in absolute coordinates (position) and speed. The API shall be independent of the underlying motor protocol, signal format, or driver hardware.

**R6 - Interpolation**: The controller shall be responsible for interpolating between an actuator's current position and a commanded target position at the commanded speed. The base station shall not need knowledge of intermediate steps or motor-specific control signals.

**R7 - Asynchronous Command Execution**: The controller shall process commands asynchronously, allowing simultaneous execution of commands across independent actuators and devices. A command issued to one actuator shall not block execution of commands on other actuators.

**R8 - Action Modes**: Each command to an actuator shall specify one of two execution modes:
  - (a) **Queue**: Append the command to the actuator's command queue. Commands execute sequentially in FIFO order.
  - (b) **Override**: Cancel all pending commands for that actuator, halt current motion, and execute the new command immediately.

**R9 - Coordinated Motion**: The controller shall support grouped commands that synchronize motion across multiple actuators. When a coordinated command group is issued, the controller shall execute the commands such that all actuators in the group begin and (where possible) complete their motions together.

### 2.3 Audio

**R10 - Audio Output (Hybrid Model)**: The controller shall support two modes of audio output through the speaker:
  - (a) **Streaming**: The base station streams audio data to the controller, which buffers and plays it in real time. This supports low-latency playback where generation and playback overlap (e.g., TTS).
  - (b) **Discrete Clip**: The base station transmits a complete audio payload. The controller plays it upon full receipt or upon explicit play command.

  The speaker shall be treated as an addressable device subject to the same queue/override semantics as actuators (R8).

**R11 - Audio Input**: The controller shall capture audio from a microphone and transmit it to the base station as a stream.

### 2.4 Video

**R12 - Video Input**: The controller shall capture video from the camera and transmit it to the base station as a stream.

### 2.5 Communication

**R13 - Base Station Communication**: The controller shall communicate bidirectionally with a base station. The communication protocol and transport shall be abstracted so that the physical mechanism (UART, SPI, Wi-Fi, Ethernet, etc.) can be changed without modifying controller logic or command processing.

**R14 - Command Protocol**: The controller shall implement a custom binary command protocol supporting:
  - (a) Actuator commands (position, speed, action mode).
  - (b) Coordinated motion groups.
  - (c) Audio output (streaming and discrete).
  - (d) Telemetry requests.
  - (e) System commands (e.g., emergency stop -- see R21).
  - (f) Protocol version handshake at connection time (see R29).

**R15 - Command Acknowledgment**: Every command received by the controller shall be acknowledged with an accept or reject response. Rejection shall include a reason code (e.g., out of range, invalid actuator ID, system in emergency stop). Command completion is not explicitly acknowledged; it is inferred from telemetry indicating the actuator has reached idle state or is executing the next queued command.

**R16 - Telemetry and Status Reporting**: The controller shall report device state to the base station via telemetry messages, including:
  - (a) Actuator state: current position, target position, currently executing command, queue depth, active/idle status.
  - (b) Sensor state: operational status, error conditions.
  - (c) Fault conditions: actuator stall, sensor timeout, communication errors.
  - (d) System health: self-test results (see R17, R18).

### 2.6 Startup and Initialization

**R17 - Actuator Homing Behavior**: Each actuator's configuration shall declare a homing behavior, one of:
  - (a) **None**: No action on startup. Suitable for actuators where position is irrelevant or retained (e.g., continuous rotation motors).
  - (b) **Return to default**: Move to a configured default position on startup. Suitable for servos and actuators with absolute positioning.
  - (c) **Calibration required**: Execute a calibration sequence before accepting commands. Suitable for actuators with external position feedback that require a reference.

  The controller shall execute the declared homing behavior for each actuator during initialization.

**R18 - Power-On Self-Test (POST)**: On startup, the controller shall perform a self-test of each actuator and sensor to verify that it is responsive. Results shall be:
  - (a) Reported via LED pattern on the onboard LED.
  - (b) Transmitted to the base station as a telemetry message once communication is established.

### 2.7 Continuous Monitoring and Safety

**R19 - Continuous Built-In Test (CBIT)**: During operation, the controller shall continuously monitor for fault conditions including but not limited to: actuator stall or failure to reach target, sensor timeout or data corruption, and communication loss with the base station. Detected faults shall be reported via telemetry (R16) and indicated via the onboard LED.

**R20 - Actuator Limits**: The controller shall enforce actuator range limits as defined in the actuator configuration (R2c). Commands that specify positions outside the configured range shall be rejected, and an error shall be reported via telemetry.

**R21 - Emergency Stop**: The controller shall support an emergency stop command that immediately halts all actuator motion, clears all command queues, and places the system in a safe state. The emergency stop shall take priority over all other commands.

### 2.8 Latency

**R22 - Low Latency Operation**: The system shall be designed to minimize latency across all paths (command execution, audio, video) such that the robot behaves as an embodied presence rather than a remote-controlled device. Buffering and processing strategies shall prioritize responsiveness.

**R23 - Update Loop Period**: The controller's main loop shall execute at a period of 1 ms or less. All tick functions (actuator interpolation, sync management, telemetry, system monitoring) shall complete within this period. This ensures smooth actuator interpolation and responsive command processing.

### 2.9 Actuator Behavior

**R24 - Actuator Hold Behavior**: Each actuator's configuration shall declare a hold behavior defining what occurs when the actuator reaches its target and the command queue is empty:
  - (a) **Active hold**: Maintain position, drawing power as needed (e.g., servo holding torque).
  - (b) **Passive release**: De-energize the actuator, allowing it to move freely (e.g., to save power or reduce wear).

  The hold behavior is a property of the actuator configuration and may differ per actuator type.

### 2.10 Power Management

**R25 - Low Power Operation**: The controller shall utilize low-power modes whenever possible, including but not limited to: clock gating unused peripherals, entering sleep states during idle periods, and de-energizing actuators according to their hold behavior (R24). Detailed power management strategies shall be developed as the design matures.

### 2.11 Connection Lifecycle

**R26 - Disconnect Behavior**: Upon detecting loss of communication with the base station, the controller shall execute a configurable stored procedure (e.g., return actuators to default positions, enter idle pose, de-energize). The stored procedure shall be defined at compile time with provisions for future runtime configurability.

**R27 - Reconnect State Push**: Upon re-establishing communication with the base station, the controller shall immediately transmit its full current state, including: all actuator positions and statuses, sensor statuses, fault conditions, POST results, and the currently loaded configuration.

### 2.12 Command Synchronization

**R28 - Internal Command Synchronization**: The controller shall maintain an internal time base sufficient to synchronize commands across actuators and devices. Coordinated behaviors (e.g., jaw movement synchronized with audio output) shall be achieved by the host sending incremental commands aligned with data packets (e.g., jaw position updates accompanying each audio chunk). Cross-system clock synchronization is handled separately (see R31).

### 2.13 Protocol Versioning

**R29 - Protocol Version Handshake**: At connection establishment, the controller and host shall exchange protocol version identifiers. The controller shall reject communication from an incompatible protocol version and report the mismatch via telemetry and LED indication.

### 2.14 Transport

**R30 - Initial Transport**: The first transport implementation shall be UART. The PAL transport interface (R1, R13) shall be implemented over UART as the reference transport for development and testing. The UART configuration (baud rate, pin assignment) shall be defined in the platform abstraction layer.

### 2.15 Clock Synchronization

**R31 - Cross-System Clock Synchronization**: The controller shall support hardware-assisted clock synchronization with the companion computer to enable correlation of controller telemetry with SBC-side data (e.g., video frames, audio timestamps). The mechanism shall use:
  - (a) A dedicated GPIO sync line between the companion computer and the controller.
  - (b) A hardware timer input capture on the controller to timestamp incoming sync pulses with cycle-accurate precision, independent of software latency.
  - (c) A protocol exchange where the companion computer reports its own timestamp for the pulse, and the controller reports its hardware-captured timestamp, enabling the companion to compute the clock offset.
  - (d) Periodic sync pulses (implementation-defined interval, e.g., 1-10 seconds) to track clock drift.

  All telemetry messages shall include a controller timestamp (microseconds since boot) so that the companion computer can map controller events to its own time domain using the established offset.

---

## 3. Design Notes

### 3.1 Custom Binary Protocol

The controller uses a custom binary protocol (R14) rather than ROS2-native serialization (CDR/DDS). The target MCU lacks the resources for micro-ROS (~32KB RAM minimum vs. 8KB available). A ROS2 driver node on the companion computer or base station will translate between the custom protocol and ROS2 topics/services. This keeps the embedded firmware simple and transport-agnostic.

### 3.2 Future: Self-Describing Robots

The current design assumes device configuration is known at compile time (R4). A future revision should consider a discovery protocol where the controller advertises its actuator and sensor inventory to the base station at connection time. This would enable a generic base station / ROS2 driver that works with any robot conforming to the protocol, rather than requiring per-robot configuration. ROS2's parameter and discovery infrastructure could support this naturally.

### 3.3 Firmware Update

Firmware update (e.g., bootloader support) is out of scope for the initial design. It can be added later as a separate concern.

### 3.4 MCU Constraints

The current target MCU (STM32L031K6, 32KB Flash, 8KB RAM) is severely resource-constrained. Streaming audio/video and running multiple interpolation loops concurrently may require upgrading to a more capable MCU, or offloading media streaming to the companion processor. The hardware abstraction layer (R1) supports this architectural flexibility.
