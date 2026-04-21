# Prototype 1 (P1) Head -- Requirements

## 1. Overview

This document defines the prototype-specific requirements for the P1 head: a 3D-printed humanoid bust with four smart serial servo actuators (neck yaw, eye horizontal, eye vertical, jaw), a camera, microphone, speaker, an STM32G071RB embedded controller, and an NVIDIA Jetson Orin Nano SBC.

### 1.1 Scope

These requirements cover the P1 head as a complete integrated prototype. They capture only what is specific to P1 and cannot be derived from the platform-agnostic requirements (MCU-R*, AI-R*). Platform-level requirements apply as-is unless explicitly deviated (Section 3).

P1 implements the platform's two-tier control architecture ([architecture.md](../../system/architecture.md) Section 7): LLM inference (roz_ai) at 1-5 Hz produces semantic motor skill directives (AI-R13), roz_control at 1 kHz translates them into actuator commands, and the controller firmware drives smart serial servos via the wire protocol. The aspirational three-tier sensorimotor architecture described in [sensorimotor_architecture.md](../../system/sensorimotor_architecture.md) is a future evolution; P1 does not implement the sensorimotor tier.

### 1.2 Reference Documents

- [P1 design](design.md)
- [Controller requirements](../../controller/requirements.md) (MCU-R*)
- [AI system requirements](../../ai/requirements.md) (AI-R*)
- [Wire protocol specification](../../protocol/wire_protocol.md)
- [System architecture](../../system/architecture.md)
- [Sensorimotor architecture](../../system/sensorimotor_architecture.md) -- future three-tier evolution
- [Embodied interaction considerations](../../system/embodied_interaction_considerations.md)

---

## 2. Requirements

### 2.1 Behavioral Realism

**P1-R1 - Response Latency**:
  - (a) The system shall begin speech output within 1500 ms of the end of a human utterance.
  - (b) The system shall begin LLM-directed gaze shifts within 500 ms of the triggering stimulus.

**P1-R2 - Idle Animation**:
  - (a) The control policy shall produce ambient motion (gaze drift, micro-saccades, small head movements) when no directive is active.
  - (b) Any LLM directive shall preempt idle motion within one control policy tick.
  - (c) Idle motion shall not produce audible servo noise at 1 m distance.

**P1-R3 - Jaw-Audio Synchronization**:
  - (a) During speech output, jaw motion shall track the audio amplitude envelope within 50 ms temporal alignment.
  - (b) Jaw motion shall cease within 100 ms of speech audio stopping.

### 2.2 System Integration

**P1-R4 - Media Routing**: Audio and video shall be routed to the SBC, not through the controller or controller link.

**P1-R5 - Controller Link**:
  - (a) The SBC shall communicate with the controller via SPI at a 1 kHz bidirectional exchange rate.
  - (b) Each exchange shall complete within 500 us.

### 2.3 Actuators

**P1-R6 - Actuator Type**: Each actuator shall be a smart serial servo with:
  - (a) 12-bit or better position feedback readable via the servo bus protocol.
  - (b) An internal control loop running at 1 kHz or faster.
  - (c) Load, temperature, and error status readable via the bus protocol.

**P1-R7 - Actuator Performance**:
  - (a) Each eye servo shall complete a 60-degree movement in under 100 ms.
  - (b) The neck servo shall complete a 60-degree movement in under 300 ms.
  - (c) The jaw servo shall track position commands at 50 Hz.

### 2.4 Power

**P1-R8 - Power Supply**:
  - (a) The system shall accept a single DC input (7.4-12V).
  - (b) The system shall provide a regulated 5V rail with current capacity for simultaneous stall on at least two 5V-rated servos.
  - (c) The system shall provide a regulated 12V rail with current capacity for the neck servo at peak load.
  - (d) The MCU shall be powered from a regulated 3.3V rail isolated from servo transients.

### 2.5 Calibration

**P1-R9 - Actuator Calibration**:
  - (a) Per-servo minimum, maximum, and center positions shall be measured and stored as calibration constants after mechanical assembly.
  - (b) The neutral pose (all actuators at calibrated center) shall be mechanically safe and visually acceptable as a rest position.

**P1-R10 - Gaze Calibration**: The system shall map gaze target coordinates (bearing, elevation) to coordinated eye and neck actuator positions, accounting for the camera-to-eye offset.

### 2.6 Mechanical

**P1-R11 - Eye Mechanism Backlash**: Total backlash in the eye mechanism (servo gears + mechanical coupling) shall be less than 1 degree per axis.

**P1-R12 - Servo Noise**: Servo noise shall not be audible at 1 m distance during idle periods.

### 2.7 Safety

**P1-R13 - Safety Constraints**:
  - (a) Soft limits shall maintain at least 5 degrees margin from each actuator's mechanical hard stop (specializes MCU-R20).
  - (b) The system shall not hold maximum torque on a stalled actuator indefinitely (specializes MCU-R19).
  - (c) Transition to safe pose shall use a controlled trajectory, not an instantaneous snap (specializes MCU-R26).
  - (d) Emergency stop shall halt all actuator motion and cease speech output within 20 ms of the command reaching the controller (specializes MCU-R21).

---

## 3. Platform Requirement Deviations

The following platform requirements are intentionally deferred or deviated from in P1, with justification.

### 3.1 MCU-R11 / MCU-R12 -- Audio and Video Input on Controller

**Platform requirement:** The controller shall capture audio from a microphone (MCU-R11) and capture video from a camera (MCU-R12) and transmit them to the base station.

**P1 deviation:** Audio and video are captured directly by the SBC, not by the controller (P1-R4). The STM32G071RB (Cortex-M0+) lacks USB host, CSI, and sufficient bandwidth for media streaming. The Jetson Orin Nano has dedicated high-bandwidth interfaces for media.

**Reconciliation:** MCU-R11/R12 should be updated to reflect that media routing depends on the system topology. When an SBC is present, media may be routed directly to the SBC.

### 3.2 MCU-R30 -- Initial Transport (UART to SPI)

**Platform requirement:** The first transport implementation shall be UART (MCU-R30).

**P1 deviation:** P1 uses SPI as the primary transport (P1-R5). UART at 921,600 baud reaches ~88% utilization at 1 kHz coordinated commands. SPI at 4 MHz provides ~500 KB/s at ~13% utilization with full-duplex operation. See [design.md](design.md) Section 2.5 and design note 4.2 for bandwidth analysis.

**Reconciliation:** MCU-R30 specifies UART as the *first* transport, and the PAL abstraction (MCU-R1, MCU-R13) supports transport changes. UART remains available as a fallback for development.

### 3.3 MCU-R31 -- Cross-System Clock Synchronization

**Platform requirement:** The controller shall support hardware-assisted clock synchronization with the companion computer (MCU-R31).

**P1 deviation:** P1's 1 kHz SPI exchange provides implicit synchronization sufficient for 1-5 ms temporal precision. See [design.md](design.md) Section 2.5.

**Reconciliation:** MCU-R31 remains valid for future prototypes requiring sub-millisecond event correlation or multi-controller coordination.

---

## 4. Design Notes

### 4.1 Why 1 kHz Requires Smart Servos

A 20 ms saccade gets 20 trajectory points at 1 kHz vs. 1 at 50 Hz. But 1 kHz commands only produce 1 kHz motion quality if the actuator can respond at 1 kHz. A hobby servo's internal PID runs at ~50 Hz regardless of command rate. Smart serial servos run their internal PID at 1 kHz, so each new target produces a distinct motor correction. This is why P1-R6b requires 1 kHz internal control loops.

### 4.2 Why SPI Instead of UART

At 1 kHz coordinated commands (~53 bytes/message), UART at 921,600 baud is at 88% utilization. SPI at 4 MHz provides ~500 KB/s at ~13% utilization, full-duplex, with ~128 us per transfer vs ~700 us for UART. The PAL transport abstraction (MCU-R1) makes this a PAL-layer change with zero impact on protocol or application logic.

### 4.3 Smart Servos Eliminate Multiple Review Concerns

Smart serial servos address several risks identified in review.md: ADC noise from servo power coupling, servo pot tapping, missing calibration story, and mechanical backlash. Position feedback is digital (no ADC), integrated (no pot tapping), factory-calibrated, and uses metal gears (< 1 deg backlash).

### 4.4 Future: Sensorimotor Architecture

P1's hardware (SPI link, smart serial servos, 1 kHz command rate) is designed to support the [sensorimotor architecture](../../system/sensorimotor_architecture.md) in a future iteration. P1 validates the control link and actuator subsystem that the sensorimotor tier would build on.
