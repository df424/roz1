# Prototype 1 (P1) Head -- Design Document

## 1. Overview

This document defines the module structure, interfaces, data flow, and algorithms for the P1 head prototype. It describes **how** the P1 realizes the requirements defined in [requirements.md](requirements.md). Hardware selections, peripheral configuration, bandwidth analysis, and control loop details are in [implementation.md](implementation.md).

### 1.1 System Decomposition

The P1 system has three major subsystems, corresponding to the platform's two-tier control architecture ([architecture.md](../../system/architecture.md) Section 7):

| Subsystem | Location | Tier | Key Modules |
|---|---|---|---|
| AI + Perception | SBC (GPU) | Cognitive (1-5 Hz) | LLM inference, perception pipeline, behavior manager, TTS |
| Motor Control | SBC (CPU) | Control policy (1 kHz) | roz_control: motor skill policies, trajectory generators, jaw-audio sync, calibration |
| Controller | STM32 | Execution (1 kHz+) | Actuator manager, PAL (SPI slave, servo bus UART, GPIO), telemetry, system manager |

### 1.2 Reference Documents

- [P1 requirements](requirements.md) (P1-R*)
- [P1 audio requirements](audio/requirements.md) (P1A-R*)
- [P1 audio design](audio/design.md)
- [P1 implementation specification](implementation.md)
- [Sensorimotor architecture](../../system/sensorimotor_architecture.md) -- aspirational three-tier evolution (not implemented in P1)
- [Controller module design](../../controller/module_design.md)
- [Controller requirements](../../controller/requirements.md) (MCU-R*)
- [AI system requirements](../../ai/requirements.md) (AI-R*)

---

## 2. Motor Skills and Control Policy Design

P1 implements the platform's two-tier control architecture: the cognitive tier (roz_ai) produces semantic motor skill directives at 1-5 Hz, and the control policy (roz_control) translates them into coordinated actuator commands at 1 kHz. The controller firmware receives these commands via SPI and drives the smart serial servos.

P1's hardware and control link are designed to support the [sensorimotor architecture](../../system/sensorimotor_architecture.md) in a future evolution, but P1 does not implement the sensorimotor tier's reactive controllers or priority arbitration. All motor behavior in P1 originates from LLM directives processed through roz_control's motor skill policies.

### 2.1 Motor Skill Policies

roz_control implements motor skill policies that translate LLM directives (AI-R13) into actuator command sequences. Each policy is a compiled C/C++ module behind roz_control's control policy interface.

**P1 motor skill policies:**

| Policy | LLM Directive | Output | Rate |
|---|---|---|---|
| Gaze | `look_at(bearing, elevation)` | Eye H, Eye V, Neck yaw positions | 1 kHz trajectory |
| Jaw sync | (automatic during TTS) | Jaw position from audio envelope | 1 kHz, synced to audio |
| Idle | (automatic when no directive active) | Ambient gaze drift, micro-movements | 1 kHz |
| Expression | `expression(type, intensity)` | Jaw + gaze coordinated pattern | 1 kHz trajectory |

### 2.2 Data Flow

```
Cognitive Tier (roz_ai, 1-5 Hz)
    │
    │  MotorDirective(skill, params)
    │  e.g., look_at(bearing=30, elevation=5)
    │
    ▼
┌──────────────────────────────────────────────┐
│  roz_control (1 kHz control policy loop)     │
│                                              │
│  1. Receive directive from roz_ai            │
│  2. Select motor skill policy                │
│  3. Generate/advance trajectory              │
│  4. Apply gaze mapping (degrees -> norm)     │
│  5. If TTS active: compute jaw from audio    │
│  6. If idle: generate ambient motion         │
│  7. Build coordinated command frame          │
└──────────────────┬───────────────────────────┘
                   │
                   ▼
              SPI to controller (1 kHz)
                   │
                   ▼
              Controller (STM32, 1 kHz+)
              Interpolation, servo bus I/O
```

### 2.3 Trajectory Generation

roz_control generates smooth trajectories for LLM-directed movements. The LLM specifies *where* to look; roz_control determines *how* to get there.

**Saccade trajectory** (eye movements, gaze shifts):
- Asymmetric velocity profile: fast acceleration, slower deceleration
- Duration scales with amplitude (~20-80 ms for typical gaze shifts)
- Small overshoot (~5%) followed by correction for naturalism

**Min-jerk trajectory** (neck movements, slow gaze shifts):
- Smooth velocity profile with zero jerk at endpoints
- Duration: 200-500 ms depending on amplitude
- Used for deliberate head turns directed by the LLM

**Idle animation** (P1-R2):
- Low-amplitude gaze drift (Perlin noise or similar)
- Occasional micro-saccades (~1-2 deg, random intervals)
- Small head movements (~5 deg, slow)
- Lowest priority -- any LLM directive preempts immediately (P1-R2b)
- Amplitude constrained to prevent audible servo noise (P1-R2c)

### 2.4 Gaze Mapping

For gaze directives, roz_control maps a target bearing/elevation to actuator positions. P1's eye-head coordination follows the natural "eyes lead, head follows" pattern:

```
Input: look_at(bearing=45 deg right)

1. Eyes: saccade trajectory to 45 deg right (or eye limit)
2. Neck: min-jerk trajectory to 30 deg right, starting ~50 ms after eyes
3. As neck moves, eyes compensate:
   eye_target = gaze_target - current_neck_angle
4. Final state: neck at 30 deg, eyes at 15 deg = 45 deg net gaze
```

The split between eye and head contribution uses the calibrated gaze parameters (P1-R10). Gaze targets within the eye range (~55 deg from center) are handled by eyes alone; larger shifts recruit the neck.

### 2.5 SPI Exchange and Execution Rate

The roz_control loop and controller both run at 1 kHz minimum (MCU-R23). The SPI exchange occurs once per control policy tick. If the controller runs faster than 1 kHz (e.g., 10 kHz, recommended per controller design note 3.5), it performs inter-setpoint interpolation between SPI exchanges.

```
SBC (SPI master, 1 kHz)                 STM32 (SPI slave, 1 kHz+)
    │                                        │
    │  1. Control policy tick (every 1 ms): │  1. Execution tick (every tick):
    │     - Check for new LLM directives     │     - Interpolate toward target
    │     - Advance active trajectory         │     - Monitor faults
    │     - Compute jaw from audio envelope  │
    │     - Generate idle motion (if idle)   │  2. On SPI exchange (every 1 ms):
    │     - Apply gaze mapping               │     - Read SPI RX DMA buffer
    │     - Build command frame              │     - Parse command, update targets
    │                                        │     - Load telemetry into SPI TX
    │  2. SPI exchange (~128 us):            │     - DMA handles transfer
    │     MOSI: command frame ──────────►    │
    │     MISO: ◄────────────── telem frame  │  3. Servo bus I/O (scheduled):
    │                                        │     - Sync write positions
    │  3. Process received telemetry         │     - Read status (round-robin)
    │     - Update position feedback state   │
    │     - Feed back to trajectory gen      │
    │                                        │
    ▼  (next tick in ~872 us)               ▼  (next tick)
```

The SPI exchange takes ~128 us at 4 MHz for a 128-byte bidirectional transfer (64 bytes command + 64 bytes telemetry, simultaneously). At 1 kHz, this leaves ~872 us per tick for processing and servo bus I/O. At higher execution rates (e.g., 10 kHz), the controller interpolates between SPI exchanges, producing smoother motion. Servo bus I/O is scheduled based on bus bandwidth (see MCU-R32, controller design note 3.6).

---

## 3. Calibration Design

### 3.1 Calibration Data Structure

Smart serial servos report position in absolute servo units (0-4095 for 12-bit resolution). Calibration maps between three coordinate spaces:

1. **Degrees** -- used by roz_control (physical angles)
2. **Normalized 0.0-1.0** -- used in the wire protocol between SBC and controller
3. **Servo units (0-4095)** -- used by the controller to command servos

```
calibration:
  actuators:
    neck_yaw:
      servo_id: 3               # Dynamixel bus ID
      servo_min: 410            # servo units at soft limit (P1-R13a)
      servo_max: 3686
      servo_center: 2048        # neutral pose position (P1-R9b)
      min_deg: -85.0            # physical angle at servo_min
      max_deg: 85.0
      center_deg: 0.0
    eye_h:
      servo_id: 1
      servo_min: 820
      servo_max: 3276
      servo_center: 2048
      min_deg: -55.0
      max_deg: 55.0
      center_deg: 0.0
    eye_v:
      servo_id: 2
      # ... same structure
    jaw:
      servo_id: 4
      servo_min: 1229           # closed
      servo_max: 2867           # fully open
      servo_center: 1229        # rest = closed
      min_deg: 0.0
      max_deg: 30.0
      center_deg: 0.0

  gaze:
    camera_offset_h_deg: 2.0   # camera is 2 deg right of eye center
    camera_offset_v_deg: -1.0  # camera is 1 deg below eye center
    eye_range_h_deg: 110.0     # total eye H range
    eye_range_v_deg: 110.0
    neck_range_deg: 170.0
```

Stored as YAML on the SBC filesystem. Loaded at startup by roz_control and pushed to the controller as actuator limits during initialization.

### 3.2 Calibration Procedure

1. **Per-servo range finding:** Using the servo bus, command each servo through its range in small increments. Read position feedback at each step. Record the servo unit positions where mechanical hard stops are reached (stall detected via load feedback). Set soft limits 5+ degrees inside these points (P1-R13a).
2. **Center/neutral calibration:** Manually position each joint to the desired neutral pose. Read servo positions via the bus. These become servo_center values and the safe pose (P1-R13c).
3. **Angle mapping:** At center and both soft limits, measure the physical angle (protractor or known fixture). Compute the linear mapping coefficients (deg = a * servo_units + b). Smart servos have highly linear position sensors, so a linear mapping is typically sufficient.
4. **Gaze calibration:** Point a known target at a known bearing. Command eyes to track it. Measure and record the camera-to-eye offset.

### 3.3 Unit Conversions

roz_control works in degrees. The output module converts to normalized 0.0-1.0 before sending commands via the wire protocol. The controller converts normalized positions to servo units for bus commands:

```python
def deg_to_norm(deg: float, cal: ActuatorCal) -> float:
    """Convert physical degrees to normalized position (roz_control -> wire protocol)."""
    norm = (deg - cal.min_deg) / (cal.max_deg - cal.min_deg)
    return clamp(norm, 0.0, 1.0)
```

```c
// Controller-side conversion (wire protocol -> servo bus)
uint16_t norm_to_servo(float norm, const actuator_cal_t *cal) {
    uint16_t pos = cal->servo_min + (uint16_t)(norm * (cal->servo_max - cal->servo_min));
    if (pos < cal->servo_min) pos = cal->servo_min;
    if (pos > cal->servo_max) pos = cal->servo_max;
    return pos;
}
```

---

## 4. Actuator and Electrical Design

### 4.1 Actuator Selection

| Joint | Recommended Servo | Torque | Weight | Bus Speed | Resolution | Gear Type | Approx. Cost |
|---|---|---|---|---|---|---|---|
| Eye H | Dynamixel XL330-M288-T | 0.52 Nm | 18 g | 4 Mbps TTL | 12-bit (4096 pos) | Metal | ~$24 |
| Eye V | Dynamixel XL330-M288-T | 0.52 Nm | 18 g | 4 Mbps TTL | 12-bit (4096 pos) | Metal | ~$24 |
| Neck yaw | Dynamixel XL430-W250-T | 1.5 Nm | 57 g | 4 Mbps TTL | 12-bit (4096 pos) | Metal | ~$50 |
| Jaw | Dynamixel XL330-M077-T | 0.18 Nm | 18 g | 4 Mbps TTL | 12-bit (4096 pos) | Metal | ~$24 |

**Lower-cost alternative:** Feetech SCS0009 (eyes/jaw, ~$8 each, 1 Mbps) and STS3215 (neck, ~$15, 1 Mbps). Same bus architecture, lower performance, significantly lower cost for initial prototyping.

**Why these actuators:**
- **XL330 for eyes:** 18g is light enough for an in-head eye mechanism. 0.52 Nm is adequate for small eye masses. 4 Mbps bus allows fast command/response cycles. Metal gears with <0.5 deg backlash.
- **XL430 for neck:** 1.5 Nm handles head mass with margin. Same protocol family as eyes. 57g weight is acceptable in a fixed neck mount.
- **XL330-M077 for jaw:** Lightest option, adequate torque for a jaw linkage, minimal space.

### 4.2 Servo Bus Architecture

Smart serial servos communicate via half-duplex TTL UART. Multiple servos share a single bus (daisy-chained). The controller manages bus arbitration: it sends a command, then switches to receive mode for the servo's response.

```
STM32G071RB
    │
    │  USART1 (half-duplex, 4 Mbps)
    │  TX/RX on single wire via tri-state buffer
    │
    ├──► Level shifter (3.3V ↔ 5V)
    │
    └──► Servo TTL Bus ──► Eye H (ID 1)
                         ──► Eye V (ID 2)
                         ──► Neck yaw (ID 3)
                         ──► Jaw (ID 4)
```

**Bus timing at 4 Mbps** (see controller requirements design note 3.6 for full analysis):

| Operation | Time |
|---|---|
| Sync write, 4 servos | ~78 us |
| Status read, 1 servo | ~125 us |

At the 10 kHz execution rate (100 us tick), sync writes (~78 us) fit in a single tick. Status reads (~125 us) span ~1.5 ticks and are staggered (round-robin, one servo per read cycle).

**P1 bus configuration:** Per MCU-R32, P1 supports two options. The choice is made at build time based on prototyping results:

**Option A: Single shared bus** -- simpler wiring, adequate if servo timing validates.
```
USART1 ──► Eye H (ID 1) ──► Eye V (ID 2) ──► Neck (ID 3) ──► Jaw (ID 4)
```

**Option B: Parallel buses** -- dedicated eye bus for lowest-latency saccade commands.
```
USART1 ──► Eye H (ID 1) ──► Eye V (ID 2)     (fast actuators, dedicated)
USART3 ──► Neck (ID 3)  ──► Jaw (ID 4)       (slower actuators, shared)
```

Option B is recommended if eye saccade latency is measurably better with a dedicated bus (expected ~40 us write for 2 servos vs ~78 us for 4).

### 4.3 Power Architecture

```
DC Input (7.4-12V, 2S-3S LiPo or bench supply)
    │
    ├──► 12V Buck/Boost Regulator ──► 12V Servo Rail
    │    (if input < 12V, boost;        100 uF bulk cap
    │     if input = 12V, pass-through) ├──► Neck (XL430-W250-T, 6.5-12V)
    │                                   └──► (future higher-voltage actuators)
    │
    ├──► 5V Buck Regulator ──► 5V Servo Rail
    │    (high current, 3A+)    100 uF bulk cap
    │                           ├──► Eye H (XL330-M288-T, 5V)
    │                           ├──► Eye V (XL330-M288-T, 5V)
    │                           └──► Jaw (XL330-M077-T, 5V)
    │
    └──► 3.3V LDO (from 5V rail) ──► MCU (STM32G071RB)
         10 uF + 100 nF               ├──► SPI to Jetson (3.3V level)
                                       ├──► Level shifter for servo bus
                                       └──► Status LED, DATA_READY GPIO
```

Key design rules (implements P1-R8):

1. **Dual servo rails.** The XL330 family is rated for 5V only; the XL430 is rated 6.5-12V. A single supply rail cannot safely power both. The 5V rail and 12V rail are independently regulated from the DC input.

2. **No ADC signal conditioning needed.** Smart servos report position digitally via the TTL bus. The analog noise concerns identified in the review (ground bounce, ADC reference instability, voltage level mismatch) are eliminated entirely.

3. **Level shifting.** The servo TTL bus runs at 5V logic. The STM32G071RB is 3.3V. A tri-state buffer with explicit direction control (e.g., 74LVC2T45 with direction GPIO) on each servo bus data line handles the translation. Auto-direction level shifters (TXB-style) are not recommended at 4 Mbps.

4. **Servo power decoupling.** 100 uF bulk capacitor at each servo rail entry point. Smart servos have internal regulators and handle motor switching internally, so per-servo decoupling is less critical than with raw PWM motors.

5. **MCU power isolation.** The 3.3V LDO is fed from the 5V rail (not directly from the DC input) for a clean, stable supply. The MCU's primary digital interfaces are SPI (to Jetson) and UART (to servos) -- both digital, both noise-tolerant.

---

## 5. Controller Firmware: P1-Specific Configuration

P1 requires porting the controller firmware from the STM32L031K6 (PWM output, UART transport) to the STM32G071RB (SPI slave transport, TTL UART servo bus). Changes are confined to the PAL layer; modules above PAL are unchanged. PAL module interfaces and data flow are defined in the [controller module design](../../controller/module_design.md). This section covers the P1-specific PAL configuration: which PAL modules are needed, peripheral assignments, and timing budget.

### 5.1 PAL Modules for P1

| PAL Module | P1 Function | Notes |
|---|---|---|
| pal_transport_spi.c | SBC link (primary, SPI1 slave + DMA) | New module. DATA_READY GPIO signals telemetry availability. |
| pal_transport_uart.c | Debug / fallback (USART2) | Retained for development via NUCLEO ST-Link VCP. |
| pal_servo_bus.c | Smart servo communication (USART1, optionally USART3) | New module. Replaces pal_pwm.c. Half-duplex at 4 Mbps + direction GPIO. |
| pal_pwm.c | Not used | Smart servos replace PWM. |
| pal_adc.c | Not used | Position feedback via servo bus protocol, not ADC. |

### 5.2 Peripheral Allocation (STM32G071RB)

| Peripheral | Function | Notes |
|---|---|---|
| SPI1 (slave) | SBC link (primary transport) | MOSI/MISO/SCK/NSS + DMA |
| USART1 (half-duplex) | Servo bus A (all 4, or eyes only in Option B) | TX pin + direction GPIO, 4 Mbps |
| USART2 | Debug / fallback UART to SBC | Via NUCLEO ST-Link VCP during development |
| USART3 (half-duplex) | Servo bus B (neck + jaw, Option B only) | TX pin + direction GPIO, 4 Mbps |
| GPIO | DATA_READY to SBC | Output, active-high pulse when telemetry ready |
| GPIO | Servo bus direction control | Output, controls TX/RX tristate buffer |
| SysTick | 1 ms main loop tick | Standard |
| PA13/PA14 | SWD debug | Reserved |
| GPIO | Status LED | Output |

### 5.3 Main Loop Timing Budget (1 kHz baseline)

At the minimum 1 kHz rate (MCU-R23), all operations fit within a single 1 ms tick:

| Step | Operation | Time |
|---|---|---|
| 1 | Check SPI RX, parse command, update targets | ~5 us |
| 2 | Interpolate 4 actuators, apply soft limits | ~2 us |
| 3 | Servo bus sync_write (4 servos) | ~78 us |
| 4 | Servo bus read (1 servo, round-robin, 250 Hz/servo) | ~125 us |
| 5 | Build telemetry frame, load SPI TX, assert DATA_READY | ~10 us |
| 6 | SPI exchange (DMA, initiated by SBC) | ~128 us |
| 7 | Stall detect, fault check | ~5 us |
| | **Total / Headroom** | **~353 us / ~647 us** |

At 10 kHz (recommended, see controller design note 3.5): interpolation + monitoring (~7 us) runs every 100 us tick. Bus I/O and SPI are scheduled on specific ticks.

---

## 6. P1 Safety Design

Platform safety behavior (MCU-R19 through MCU-R21, MCU-R26) is implemented by the controller firmware's actuator manager and system manager modules (see [controller module design](../../controller/module_design.md)). This section covers P1-specific safety parameters and behavior (P1-R13).

### 6.1 Defense in Depth

Soft limits are enforced at two levels: roz_control clamps all actuator commands to calibrated soft limits before sending (catches trajectory overshoot and LLM errors), and the controller enforces MCU-R20 limits independently (catches protocol errors and SBC-side bugs). Both use the same calibration data, set during initialization.

### 6.2 P1 Stall Detection Parameters

The controller's stall detection (MCU-R19) uses position and load feedback from smart servos (P1-R6). P1-specific parameters:

- **Stall threshold:** ~5% of range (~200 servo units, adjustable per actuator)
- **Load threshold:** ~80% of rated load (indicates mechanical obstruction, not slow convergence)
- **Stall timeout:** ~500 ms (long enough to not false-trigger during fast saccades)
- **Stall response:** Transition to torque-off mode via servo bus command (P1-R13b)

Smart servos also report hardware error flags (overload, overheating) that are checked independently.

### 6.3 P1 Safe Pose Behavior

On communication loss (MCU-R26), the controller generates a min-jerk trajectory from current position to calibrated center over 500 ms (P1-R13c), then sets all servos to torque-off mode. Torque-off after reaching safe pose prevents indefinite power draw and servo buzzing.

---

## 7. Jaw-Audio Synchronization Design

### 7.1 Signal Flow

```
TTS Pipeline (SBC)
    │
    │  Audio chunks (~20-50 ms each)
    │
    ├──► Speaker (I2S)      ──► Audio output (audible)
    │
    └──► roz_control jaw    ──► Amplitude extraction
         sync policy            │
                                │  Jaw position (0.0 = closed, 1.0 = fully open)
                                │  Scaled by amplitude envelope
                                │
                                ▼
                           roz_control ──► Controller ──► Jaw servo
```

### 7.2 Envelope Extraction

```python
def extract_jaw_position(audio_chunk: np.ndarray, sample_rate: int) -> float:
    """Extract jaw opening from audio chunk amplitude."""
    # RMS energy of the chunk
    rms = np.sqrt(np.mean(audio_chunk ** 2))
    # Normalize to 0-1 range (calibrated against typical TTS output level)
    normalized = min(rms / RMS_FULL_SCALE, 1.0)
    # Apply nonlinear mapping (jaw opens more for louder speech)
    jaw_position = normalized ** 0.6  # gamma < 1 = opens more at low volume
    return jaw_position
```

### 7.3 Latency Alignment

The audio chunk is simultaneously:
1. Queued for I2S output (speaker).
2. Processed by roz_control's jaw sync policy to produce a jaw command.

Since both happen on the same SBC, the jaw command is generated at the same time the audio is queued. The jaw command must arrive at the servo before the audio arrives at the speaker. Given:
- I2S buffer: ~5-10 ms of audio queued ahead
- SPI exchange latency: ~0.128 ms (128 us at 4 MHz)
- Controller dispatch + servo bus write: ~0.2 ms (sync_write to jaw servo)
- Servo response time: ~1 ms (smart servo internal PID at 1 kHz)

The jaw command arrives at the servo ~1.3 ms after the control policy tick, while the audio plays 5-10 ms after being queued. The jaw consistently leads audio by 4-9 ms, well within the 50 ms alignment target (P1-R3a). The SPI transport's low latency (~128 us vs ~700 us for UART) makes this margin comfortable. If empirical testing shows the jaw leads noticeably, a small delay can be added to the jaw command path.

---

## 8. Design Notes

### 8.1 Why Control Policy on SBC, Not Controller

All trajectory computation (saccade profiles, min-jerk curves, idle animation, jaw-audio sync) runs on the SBC via roz_control. The controller performs only linear interpolation between successive 1 kHz setpoints. This keeps the controller firmware simple and means motor skill policies can be updated without reflashing the MCU. It also means learned policies (neural net controllers, AI-R13c) naturally run on the SBC's GPU.

### 8.2 Position Feedback Is Built-In

Unlike the hobby servo architecture (where position feedback required external ADC hardware and analog signal conditioning), smart serial servos provide position feedback as an inherent part of the bus protocol. Every servo status read returns 12-bit position, load, temperature, and error flags. This means position feedback is available from the first servo bus bring-up -- there is no separate "sensor subsystem" to build or integrate. Stall detection (P1-R13b), proprioceptive feedback to the control policy, and calibration verification are all available as soon as the servo bus is operational.

### 8.3 Future: Sensorimotor Architecture

P1's hardware and control link are designed to support the [sensorimotor architecture](../../system/sensorimotor_architecture.md) in a future evolution. The sensorimotor tier would add reactive controllers (audio orienting, face tracking, startle responses) that bypass the LLM for sub-250 ms reflexive responses, along with priority arbitration between reactive and deliberate behaviors. P1 validates the actuator subsystem, control link, and 1 kHz command rate that the sensorimotor tier would build on. The incremental adoption path described in sensorimotor_architecture.md Section 5.3 starts from P1's two-tier baseline.
