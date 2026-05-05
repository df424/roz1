# Prototype 1 (P1) Head -- Implementation Specification

This document captures all implementation-specific decisions, hardware selections, peripheral architecture, bandwidth analysis, and control loop design for the P1 head prototype. It is the bridge between the platform-agnostic requirements (what the system must do) and the concrete hardware and interfaces that realize them.

The platform-agnostic requirements live in:
- [Controller requirements](../../controller/requirements.md) (CTRL-R*)
- [AI system requirements](../../ai/requirements.md) (AI-R*)
- [Wire protocol specification](../../protocol/wire_protocol.md)
- [System architecture](../../system/architecture.md)
- [Embodied interaction considerations](../../system/embodied_interaction_considerations.md) (requirement seeds AI-HI-R*)

This document does not redefine requirements -- it specifies **how** the P1 hardware realizes them.

---

## 1. Realism Requirements

The P1 head must produce behavior that reads as alive to a human observer. This is not a subjective goal -- human social responses have measurable onset latencies that set hard deadlines. A robot that consistently reacts slower than these baselines will feel sluggish, inattentive, or broken, regardless of the quality of its verbal responses.

This section establishes the human behavioral baselines that drive every subsequent implementation decision in this document.

### 1.1 Human Response Baselines

The P1 head supports four behavioral domains: gaze (eye movement), head orientation (neck), facial motion (jaw), and speech. The table below lists the human responses relevant to these domains, with their onset latencies and durations drawn from neuroscience literature.

| Response | Onset Latency | Duration | Neural Pathway | P1 Actuators |
|---|---|---|---|---|
| Auditory startle (head retraction) | 80-120 ms | 50-100 ms | Brainstem reflex | Neck yaw |
| Saccade to sudden stimulus | 150-250 ms | 20-200 ms | Superior colliculus | Eye H, Eye V |
| Smooth pursuit initiation | 100-130 ms | Continuous | Cerebellum | Eye H, Eye V |
| Head orient to sound | 200-400 ms | 300-500 ms | Brainstem + cortex | Neck yaw, Eye H |
| Emotional facial reaction | 200-500 ms | Variable | Limbic + motor cortex | Jaw (limited) |
| Backchannel nod | 200-400 ms | 300-500 ms | Cortex (habitual) | Neck yaw |
| Verbal response | 500-1500 ms | Variable | Cortex (deliberate) | Jaw + speaker |

The onset latencies define the **maximum allowable delay** from stimulus to first visible motion for each behavior. A robot that orients to a sound in 600 ms when humans do it in 200-400 ms will feel slow, even if the final motion itself looks natural.

### 1.2 Three-Tier Mapping

These baselines reveal a fundamental problem with a two-tier control architecture (LLM at 1-5 Hz + control policy at 50-200 Hz, as defined in AI requirements Section 3.9). The LLM's inference latency is 200-500 ms. This means:

- **Responses faster than ~200 ms are impossible through the LLM.** Auditory startle (80-120 ms), saccade to stimulus (150-250 ms), and smooth pursuit initiation (100-130 ms) all require action before the LLM has produced a single token.
- **Responses in the 200-500 ms range are unreliable through the LLM.** Head orienting (200-400 ms) and backchannel nods (200-400 ms) overlap with LLM inference time, so they may or may not arrive in time depending on the LLM's current latency.
- **Only verbal responses (500-1500 ms) are comfortably within LLM timing.**

This means the two-tier architecture cannot produce human-realistic reactive behavior. The control policy at 50-200 Hz has the right timing but currently receives directives only from the LLM -- it has no direct sensor access and cannot act independently.

The solution is a **three-tier sensorimotor architecture** that adds a middle tier with direct sensor access and autonomous reactive capability:

| Tier | Rate | Location | Human Analog | What It Does | What Drives It |
|---|---|---|---|---|---|
| Cognitive | 1-5 Hz | SBC (LLM) | Cortex | Verbal responses, complex decisions, semantic motor directives | Multimodal perception, conversation, memory |
| Sensorimotor | 500 Hz | SBC (reactive + trajectory) | Brainstem / cerebellum | Reflexive orienting, gaze tracking, idle animation, jaw sync, trajectory generation | Direct sensor events (audio onset, face detection, motion) + LLM directives |
| Servo execution | 1 kHz | STM32 controller | Spinal cord | PWM output, linear interpolation, position sensing, telemetry | Actuator commands from sensorimotor tier |

The sensorimotor tier is the key addition. It runs on the SBC alongside the LLM but operates independently at 500 Hz. It has **direct access to perceptual events** (audio onset, face position, visual motion) and can **generate actuator commands without waiting for the LLM**. This enables:

- Saccade to a sudden sound within 150-250 ms (audio onset detector -> gaze command, no LLM involved).
- Smooth pursuit of a detected face at 500 Hz (face tracker -> continuous gaze updates).
- Startle response to a loud noise within 80-120 ms (energy spike -> head retraction).
- Idle micro-movements and gaze drift (continuous, no LLM involvement).
- Jaw envelope tracking during TTS (audio amplitude -> jaw commands at audio chunk rate).

When the LLM *does* produce a directive ("look at the person on the left", "nod"), the sensorimotor tier translates it into a trajectory and executes it. The LLM's directives override or modulate reactive behaviors, just as cortical control can suppress reflexes in humans.

### 1.3 Latency Budgets by Response Type

Each P1-relevant behavior maps to a tier and a latency budget. The budget is allocated across the processing stages specific to that tier's pathway.

**Reflexive responses (sensorimotor tier, no LLM):**

| Response | Budget | Pathway |
|---|---|---|
| Auditory startle | 80-120 ms | Mic buffer (~5-10 ms) + audio onset detect (~2-5 ms) + sensorimotor decision (~2 ms) + command TX (~1 ms) + controller dispatch (~1 ms) + servo response (~5-20 ms) = **~16-39 ms** -- well within budget |
| Saccade to sound | 150-250 ms | Mic buffer (~5-10 ms) + direction estimate (~10-20 ms) + saccade trajectory start (~2 ms) + command TX (~1 ms) + controller + servo (~6-21 ms) = **~24-54 ms** -- within budget |
| Smooth pursuit | 100-130 ms onset | Frame capture (~16-33 ms) + face detect (~10-20 ms) + pursuit update (~2 ms) + command TX + controller + servo (~7-22 ms) = **~35-77 ms** -- within budget |
| Face tracking saccade | 150-250 ms | Same as smooth pursuit but with saccade trajectory -- within budget |

**Habitual responses (sensorimotor tier, triggered by LLM or pattern):**

| Response | Budget | Pathway |
|---|---|---|
| Head orient to sound | 200-400 ms | Audio onset -> sensorimotor orient reflex: **~24-54 ms** (same as saccade to sound, but neck servo is slower mechanically) |
| Backchannel nod | 200-400 ms | LLM decides to nod (~200-500 ms) + sensorimotor trajectory generation (~2 ms) + execution (~50-100 ms) = **~252-602 ms** -- marginal through LLM; alternatively, pattern-matched in sensorimotor tier from prosody cues: **~50-100 ms** |

**Deliberate responses (cognitive tier, through LLM):**

| Response | Budget | Pathway |
|---|---|---|
| Verbal response | 500-1500 ms | Audio capture (~100 ms) + perception prep (~10 ms) + LLM inference (~200-500 ms) + TTS first chunk (~100-200 ms) + audio output (~5 ms) = **~415-815 ms** -- within budget |
| Complex gaze directive | 500-1500 ms | LLM inference (~200-500 ms) + sensorimotor trajectory (~2 ms) + execution -- within budget |

The key insight: reflexive and tracking behaviors fit comfortably within human baselines *only if they bypass the LLM*. Routing them through the cognitive tier would add 200-500 ms of inference latency, pushing them outside the human range.

---

## 2. Three-Tier Sensorimotor Architecture

### 2.1 Architecture Overview

The P1 system spans two processors (SBC and STM32 controller) connected by a UART link. Audio and video are routed directly to the SBC via dedicated interfaces -- not through the controller. The controller handles only actuator execution and position sensing.

```
+---------------------------------------------------------------------------+
|                          SBC (Jetson Orin Nano)                            |
|                                                                           |
|  ┌─────────────────────────────────────────────────────────────────────┐  |
|  │  COGNITIVE TIER (1-5 Hz)                                            │  |
|  │                                                                     │  |
|  │  Microphone ──► Audio pipeline ──► ┌────────────────┐               │  |
|  │  Camera ──────► Vision pipeline ─► │   AI System    │               │  |
|  │  Telemetry ──────────────────────► │   (Gemma4)     │               │  |
|  │  Long-term memory ───────────────► │                │               │  |
|  │                                    └───────┬────────┘               │  |
|  │                                            │                        │  |
|  │                              Semantic motor directives              │  |
|  │                       ("look at person", "nod", "track")            │  |
|  └────────────────────────────────────┬────────────────────────────────┘  |
|                                       │                                   |
|                                       ▼                                   |
|  ┌─────────────────────────────────────────────────────────────────────┐  |
|  │  SENSORIMOTOR TIER (500 Hz)                                         │  |
|  │                                                                     │  |
|  │  Perceptual events ──────────────────────► ┌──────────────────┐     │  |
|  │    Audio onset, energy level                │  Reactive        │     │  |
|  │    Face position, motion vectors            │  Controllers     │     │  |
|  │    Direction of arrival estimate            │  (bypass LLM)    │     │  |
|  │                                             └────────┬─────────┘     │  |
|  │                                                      │              │  |
|  │  LLM directives ──────► ┌──────────────────┐        │              │  |
|  │                         │  Trajectory       │        │              │  |
|  │  TTS audio envelope ──► │  Generators       ├────────┤              │  |
|  │                         │  (saccade, min-   │        │              │  |
|  │  Idle policy ─────────► │   jerk, idle)     │        │              │  |
|  │                         └──────────────────┘        │              │  |
|  │                                                      │              │  |
|  │  ┌─────────────────┐                                │              │  |
|  │  │  Coordination   │◄───────────────────────────────┘              │  |
|  │  │  & Arbitration  │                                               │  |
|  │  │  (priority,     │                                               │  |
|  │  │   eye-head,     │                                               │  |
|  │  │   LLM override) │                                               │  |
|  │  └────────┬────────┘                                               │  |
|  │           │                                                        │  |
|  │  Actuator commands (CoordinatedCommand at 500 Hz)                  │  |
|  └───────────┼────────────────────────────────────────────────────────┘  |
|              │                                                           |
|              │  roz_host (UART)                    ┌───────────┐         |
|              │                                     │  Speaker  │         |
|              │                                     │ (audio out)│         |
+--------------┼─────────────────────────────────────┴───────────┴─────────+
               │ UART (460.8-921.6 kbaud)
               │ commands down / telemetry up
+--------------┼───────────────────────────────────────────────────────────+
|  STM32 Controller                                                        |
|              ▼                                                           |
|  ┌────────────────────────────────────────────────────────────────────┐  |
|  │  SERVO EXECUTION TIER (1 kHz, 1 ms tick)                           │  |
|  │                                                                    │  |
|  │  1. Poll UART ──► parse incoming commands                          │  |
|  │  2. Enqueue/override actuator targets                              │  |
|  │  3. Interpolate: advance each actuator toward target               │  |
|  │  4. Write PWM CCR registers                                        │  |
|  │  5. Sample position sensors (ADC DMA)                              │  |
|  │  6. Build and send telemetry (at configured interval)              │  |
|  │  7. Monitor: stall detect, fault check, disconnect timeout         │  |
|  └────────────────────────────────────────────────────────────────────┘  |
|                                                                          |
|  TIM CH ──► Neck yaw servo            ADC CH ◄── Neck position sensor   |
|  TIM CH ──► Eye H servo              ADC CH ◄── Eye H position sensor   |
|  TIM CH ──► Eye V servo              ADC CH ◄── Eye V position sensor   |
|  TIM CH ──► Jaw servo                ADC CH ◄── Jaw position sensor     |
+--------------------------------------------------------------------------+
```

### 2.2 Why Two Tiers Are Insufficient

The current AI requirements (Section 3.9) define a two-rate loop:

- **Reasoning tier** (LLM, 1-5 Hz): produces semantic motor directives.
- **Execution tier** (control policy, 50-200 Hz): translates directives to actuator commands.

This architecture assumes all motor behavior originates from the LLM. The control policy is a *translator* -- it converts LLM directives into actuator trajectories but has no autonomous decision-making capability and no direct sensor access.

The problem is that reflexive and tracking behaviors cannot originate from the LLM because its inference latency (200-500 ms) exceeds the onset deadline (80-250 ms). Even if the control policy ran at 1 kHz, it would still wait 200-500 ms for the LLM to *decide* to orient before it could *begin* the trajectory. The bottleneck is not execution speed -- it is decision latency.

The three-tier architecture solves this by giving the middle tier (sensorimotor) both sensor access and decision authority for a defined set of reflexive behaviors. The LLM retains authority over deliberate, semantically complex behaviors. The sensorimotor tier handles everything that must happen faster than the LLM can think.

### 2.3 Tier Responsibilities

**Cognitive tier (1-5 Hz, LLM):**
- Decides *what* the robot should do based on full multimodal context (audio, vision, conversation, memory).
- Issues semantic motor directives: "look at the person at bearing 30 deg", "nod", "express surprise".
- Can override or suppress sensorimotor reflexes (e.g., "don't track that motion, keep looking at me").
- Drives verbal responses and complex multi-step behaviors.
- Does not directly produce actuator commands.

**Sensorimotor tier (500 Hz, SBC):**
- Executes two classes of behavior:
  1. **Reactive behaviors** driven by perceptual events (audio onset, face detection, visual motion) -- these bypass the LLM entirely.
  2. **Trajectory execution** of LLM directives (saccade profiles, min-jerk head turns, idle animation, jaw envelope tracking).
- Has direct access to preprocessed perceptual events (not raw audio/video -- those are too expensive to process at 500 Hz).
- Coordinates multi-actuator motion (eye-head coordination, diagonal saccades).
- Arbitrates when multiple behaviors compete for the same actuators.
- Outputs actuator commands to the controller via roz_host at up to 500 Hz.

**Servo execution tier (1 kHz, STM32 controller):**
- Receives actuator commands from the sensorimotor tier.
- Interpolates linearly between successive targets at 1 kHz (smoothing the 500 Hz command stream).
- Writes PWM registers, reads position sensors, builds telemetry.
- Monitors for stalls, faults, and communication loss.
- Has no knowledge of behaviors, trajectories, or sensor semantics -- it executes position commands.

### 2.4 System Boundary

The three-tier architecture defines a clear hardware boundary:

- **SBC (Jetson Orin Nano):** Cognitive tier + sensorimotor tier. All perception (audio, video), all decision-making (LLM + reactive controllers), all trajectory generation.
- **STM32 controller:** Servo execution tier only. Receives commands, interpolates, drives actuators, reports telemetry.
- **UART link:** Carries only actuator commands (downlink) and telemetry (uplink). No audio, no video, no perception data crosses this boundary.

This means:
- The controller does not handle audio streaming (no I2S/SAI needed on the MCU).
- The controller does not handle video (no camera interface needed).
- UART bandwidth is dedicated to actuator commands and telemetry -- no media contention.
- The sensorimotor tier's reactive controllers run on the SBC, not the controller, because they need access to perceptual events (audio onset, face detection) that are processed on the SBC.
- The controller firmware stays simple: it does not need to know about saccades, reflexes, or tracking behaviors.

---

## 3. Hardware Selections

### 2.1 Embedded Controller: STM32G071RB

The P1 controller uses the STM32G071RB (Cortex-M0+, 64 MHz, 128 KB Flash, 36 KB RAM), upgrading from the STM32L031K6 used during initial development. The G071RB is in the same Cortex-M0+ family, so firmware ports with minimal effort -- primarily PAL-layer pin and peripheral remapping.

**Why upgrade from STM32L031K6:**

| Resource | STM32L031K6 | STM32G071RB | P1 Impact |
|---|---|---|---|
| Flash | 32 KB | 128 KB | Room for protocol + oversampled telemetry + future features |
| RAM | 8 KB | 36 KB | Comfortable oversampling buffers, deeper command queues |
| Timer channels (PWM) | 4 practical | 12+ | 4 servos with 8+ spare for P1.5 expansion |
| ADC channels | 10 (5 free) | 16 (12+ free) | Position feedback on all joints + spares |
| USART | 1 + LPUART | 4 + LPUART | Debug UART without sacrificing host link |
| DMA channels | 7 | 7 | Same -- sufficient for ADC scan + UART TX/RX |
| Clock | 32 MHz | 64 MHz | 2x cycle budget per 1 ms tick |
| I2C | 1 | 2 | Dedicated bus for digital sensors without contention |
| SPI | 1 | 2 | SPI sensors without conflicting with other peripherals |
| Price (NUCLEO board) | ~$12 | ~$12 | Cost-neutral |

The G071RB is available in LQFP-64, providing 52 GPIO pins (vs. 26 on the L031K6's LQFP-32). This eliminates pin conflicts between PWM outputs, ADC inputs, and communication peripherals that constrain the L031K6.

**Development board:** NUCLEO-G071RB.

### 2.2 SBC: Jetson Orin Nano

The SBC is the NVIDIA Jetson Orin Nano (8 GB), running the AI system, motor skill policy, audio pipeline, and video pipeline.

| Parameter | Value |
|---|---|
| GPU | Ampere, 1024 CUDA cores, 32 Tensor cores |
| AI Performance | 40 TOPS (INT8) |
| CPU | 6-core Arm Cortex-A78AE |
| Memory | 8 GB LPDDR5 (68 GB/s) |
| Storage | M.2 NVMe |
| USB | 3x USB 3.2 Gen2 (10 Gbps) |
| CSI | Up to 4 lanes MIPI CSI-2 |
| 40-pin header | UART, I2S, SPI, I2C, GPIO |
| Power | 7-15 W |

### 2.3 Actuator Inventory

The P1 head has four hobby servos:

| Actuator | Function | Range | Servo Type |
|---|---|---|---|
| Neck yaw | Head left/right | ~180 deg | Standard analog |
| Eye horizontal | Left/right gaze | ~120 deg | Micro/sub-micro |
| Eye vertical | Up/down gaze | ~120 deg | Micro/sub-micro |
| Jaw | Mouth open/close | ~60 deg usable | Micro |

---

## 4. SBC Peripheral Architecture

The Jetson Orin Nano hosts audio, video, and speaker through its own interfaces -- parallel to (not through) the controller link.

### 3.1 Audio Input (Microphone to SBC)

| Parameter | Value |
|---|---|
| Interface | USB Audio Class (UAC) |
| Connection | USB microphone or USB audio adapter + electret mic |
| Sample rate | 16-48 kHz |
| Bit depth | 16-bit |
| Channels | 1 (mono) |
| Bandwidth | 32-192 KB/s (uncompressed PCM) |
| Latency | ~5-20 ms (USB audio buffer) |

USB Audio is plug-and-play on Linux and avoids consuming 40-pin header pins. For lower latency, I2S via the 40-pin header is an alternative (~1-2 ms buffer), but requires a breakout board and additional wiring.

### 3.2 Video Input (Camera to SBC)

| Parameter | Value |
|---|---|
| Interface | USB 3.2 or MIPI CSI-2 |
| Resolution | 640x480 to 1920x1080 |
| Frame rate | 10-60 fps |
| Format | MJPEG (USB) or RAW/YUV (CSI) |
| Bandwidth | 5-50 Mbit/s (compressed); up to 1.5 Gbit/s (RAW CSI) |
| Latency | ~16-100 ms (frame interval) |

CSI-2 offers lower latency and CPU-free capture via the Jetson's ISP, but requires a CSI-compatible camera module and ribbon cable. USB cameras are simpler to prototype with.

### 3.3 Audio Output (SBC to Speaker)

| Parameter | Value |
|---|---|
| Interface | USB Audio Class or I2S via 40-pin header |
| Amplifier | External class-D amp (e.g., MAX98357A for I2S, or USB audio adapter line-out to amp) |
| Sample rate | 16-48 kHz |
| Bit depth | 16-bit |
| Channels | 1 (mono) |
| Bandwidth | ~192 KB/s (48 kHz/16-bit/mono, uncompressed) |

I2S is preferred for audio output if latency matters for jaw-audio synchronization -- the audio pipeline and jaw command generation share the same SBC process, so synchronization is achieved in software without cross-device timing.

### 3.4 Controller Link (SBC to STM32)

| Parameter | Value |
|---|---|
| Interface | UART (via USB-to-UART on NUCLEO, or direct UART on 40-pin header) |
| Baud rate | 460,800 - 921,600 (see Section 7 for analysis) |
| Protocol | ROZ wire protocol (COBS framing, CRC-16) |
| Effective throughput | ~37-74 KB/s |
| Content | Actuator commands (downlink), telemetry + acks (uplink) |

During development with the NUCLEO board, the UART routes through the ST-Link's USB VCP. For production, a direct UART connection from the Jetson 40-pin header to the STM32 eliminates the USB-serial bridge and its latency.

### 3.5 Interface Summary Diagram

```
Jetson Orin Nano
+--------------------------------------------------+
|                                                    |
|  USB 3.2 port 1 <--- Microphone (USB Audio)       |
|  USB 3.2 port 2 <--- Camera (USB UVC)             |
|    or CSI-2     <--- Camera (MIPI ribbon)          |
|                                                    |
|  40-pin header:                                    |
|    I2S TX ---------> Speaker amp (MAX98357A)       |
|    UART TX/RX <----> STM32 controller              |
|    (or USB port 3 <-> NUCLEO ST-Link VCP)          |
|                                                    |
+--------------------------------------------------+
```

---

## 5. STM32G071RB Peripheral Architecture

### 4.1 Servo PWM Generation

Standard hobby servos require a PWM signal: 50 Hz (20 ms period), pulse width 1000-2000 us mapping to full range of motion. Digital servos accept up to 333 Hz.

| Option | Peripheral | Channels | Resolution | CPU Load | Notes |
|---|---|---|---|---|---|
| **Hardware PWM (selected)** | TIM1/2/3 | 12+ | 1 us at 64 MHz / PSC=64 | Near zero | Timer runs autonomously; CPU only writes CCR |
| External PWM (PCA9685) | I2C | 16 per IC | 12-bit (~1 us at 50 Hz) | Very low | Good for scaling to >4 servos; adds I2C latency |

**P1 allocation:** 4 channels from TIM2/TIM3 (or TIM1 + TIM3). The specific timer-to-pin mapping depends on the NUCLEO-G071RB board layout and will be determined during board bring-up.

**PWM timing:**

```
Timer clock:  64 MHz
Prescaler:    64 --> Timer tick = 1 us
Period:       19999 --> 20 ms = 50 Hz

CCR range:    1000 - 2000 (1 ms to 2 ms pulse width)
Resolution:   1 us per count --> 1000 discrete positions
Angular res:  ~0.18 deg/count for 180-deg servo
              ~0.12 deg/count for 120-deg servo (eyes)
```

**Higher update rates for eye servos:** Digital servos can accept 200 Hz PWM (5 ms period). At 50 Hz, a saccade lasting 20-200 ms gets only 1-10 update steps. At 200 Hz, it gets 4-40 steps -- much smoother. If digital servos are selected for the eyes, the eye timer can run at 200 Hz (Period=4999) while the neck/jaw timer remains at 50 Hz.

### 4.2 Position Feedback Sensors

The current design uses open-loop control. Adding position feedback enables stall detection (CTRL-R19), closed-loop motor skill policies (AI-R13f), calibration, and accurate telemetry.

| Sensor Type | Interface | Data Format | Sample Rate | Resolution | Cost | Notes |
|---|---|---|---|---|---|---|
| **Servo pot via ADC** | Analog | 12-bit | Up to 1 MHz | 12-bit (0.088 deg/360) | Free (if servo supports) | Requires modified servo or feedback-equipped servo |
| **External potentiometer** | Analog | 12-bit | Up to 1 MHz | 10-12 bit | $1-3 each | Mechanically coupled to joint |
| **Magnetic encoder (AS5600)** | I2C (or analog) | 12-bit angle | ~150 Hz (I2C), 1 kHz (analog) | 12-bit (0.088 deg) | $2-5 each | Contactless, compact. All share I2C addr 0x36 -- needs mux for multiple |
| **Magnetic encoder (AS5048A)** | SPI | 14-bit angle | Up to 10 kHz | 14-bit (0.022 deg) | $5-8 each | Higher resolution, SPI chip-select per device |

**P1 recommendation:** Analog feedback (servo pot tap or external potentiometer) via ADC. Simplest integration, uses the G071RB's abundant ADC channels, and supports oversampled telemetry without additional bus contention.

### 4.3 ADC Configuration

The STM32G071RB has a single 12-bit ADC with up to 16 external channels.

| Parameter | Value |
|---|---|
| Resolution | 12-bit (4096 counts) |
| Max speed | 2.5 Msps |
| Sampling mode | Scan + DMA (continuous or timer-triggered) |
| P1 channels used | 4 (one per actuator position feedback) + 1 spare |

**DMA-based scan configuration:**

```
ADC scan: 4 channels (position feedback for neck, eye_h, eye_v, jaw)
DMA: circular mode, half-word (16-bit) transfers
Buffer: uint16_t adc_buf[4]
Trigger: continuous or timer-triggered at 1 kHz (for standard telemetry)
         or timer-triggered at 10 kHz (for oversampled telemetry)

Conversion time per channel: ~2.5 us
Total scan time: ~10 us for 4 channels
DMA transfer: negligible
CPU cost: reads adc_buf[] each tick -- 4 loads, ~4 cycles
```

For oversampled telemetry (Section 8), the scan rate increases:

```
Trigger: timer at 10 kHz (100 us interval)
Buffer: uint16_t adc_buf[4][10]  (4 channels x 10 samples per telemetry window)
DMA: circular, CPU reads accumulated samples each telemetry interval
RAM cost: 80 bytes (~0.2% of 36 KB)
```

### 4.4 UART Configuration

| Parameter | Value |
|---|---|
| Peripheral | USART2 (host link) |
| Baud rate | 460,800 (recommended minimum for P1) |
| DMA | TX and RX via DMA for zero-copy frame handling |
| Flow control | None (COBS framing + CRC provides integrity) |
| Buffer | Circular DMA RX buffer, app-level TX queue |

DMA-based UART eliminates per-byte interrupt overhead and ensures the 1 ms main loop is not blocked by serial I/O. At 460,800 baud, a full telemetry frame (~80-100 bytes with oversampled data) transmits in ~2 ms, overlapping with the next main loop iterations.

### 4.5 Peripheral Allocation Map

| Pin | Peripheral | Function | Signal Type | Data Rate |
|---|---|---|---|---|
| TBD | TIM CH | Neck yaw PWM | PWM out, 50 Hz | 50 pulses/s |
| TBD | TIM CH | Eye H PWM | PWM out, 50-200 Hz | 50-200 pulses/s |
| TBD | TIM CH | Eye V PWM | PWM out, 50-200 Hz | 50-200 pulses/s |
| TBD | TIM CH | Jaw PWM | PWM out, 50 Hz | 50 pulses/s |
| TBD | ADC CH | Neck position feedback | Analog in | 1-10 kHz sample |
| TBD | ADC CH | Eye H position feedback | Analog in | 1-10 kHz sample |
| TBD | ADC CH | Eye V position feedback | Analog in | 1-10 kHz sample |
| TBD | ADC CH | Jaw position feedback | Analog in | 1-10 kHz sample |
| TBD | USART2 TX | Host serial TX | UART | 460.8-921.6 kbaud |
| TBD | USART2 RX | Host serial RX | UART | 460.8-921.6 kbaud |
| PA13 | SWD | Debug SWDIO | Digital | Debug only |
| PA14 | SWD | Debug SWCLK | Digital | Debug only |
| TBD | GPIO | Status LED | Digital out | N/A |
| TBD | I2C1 SCL | Digital sensors (future) | I2C | 100-400 kHz |
| TBD | I2C1 SDA | Digital sensors (future) | I2C | 100-400 kHz |

Pin assignments marked TBD will be determined during NUCLEO-G071RB board bring-up based on the board's physical pin breakout and CubeMX alternate function mapping. The G071RB's 52 GPIO pins (LQFP-64) provide ample flexibility -- there are no pin conflicts for the P1 peripheral set.

---

## 6. Control Loop Architecture

The full control loop spans two processors across three tiers (see Section 2 for architecture overview). The SBC runs the cognitive tier (LLM) and sensorimotor tier (reactive controllers + trajectory generation). The controller runs the servo execution tier (interpolation + PWM output).

### 5.1 Trajectory Generation: SBC-Side

The sensorimotor tier computes trajectory profiles (saccade, min-jerk, idle drift) and sends a stream of position targets at up to 500 Hz. The controller interpolates linearly between successive targets at 1 kHz, smoothing the 500 Hz command stream into continuous motion.

**Why trajectory on SBC, not controller:**
- Controller firmware stays simple (linear interpolation only).
- Trajectory logic is easy to change on the SBC (Python, no reflash needed).
- Learned policies (neural net controllers) naturally live on the SBC's GPU.
- Reactive controllers need sensor access (audio events, face detections) that only exists on the SBC.

**Trade-off:** Depends on command stream arriving reliably. If a command is late or lost, the controller holds the last position (brief stutter). At 500 Hz command rate, a single dropped command causes at most 2 ms of stall -- imperceptible. The controller's 1 kHz interpolation bridges any gaps.

### 5.2 Latency Budgets (Stimulus to Motion)

The three-tier architecture produces different end-to-end latencies depending on which tier initiates the behavior. Section 1.3 provides detailed latency budgets for each response type. Summary:

| Path | Stimulus | First Motion | Total Latency | Bottleneck |
|---|---|---|---|---|
| Reflexive (sensorimotor) | Audio onset | Gaze saccade | **~24-54 ms** | Mic buffer + audio processing |
| Reflexive (sensorimotor) | Face detected | Smooth pursuit | **~35-77 ms** | Frame interval + face detection |
| Deliberate (cognitive) | Speech input | Verbal response | **~415-815 ms** | LLM inference + TTS |
| Deliberate (cognitive) | Speech input | Gaze directive | **~210-510 ms** | LLM inference |
| Execution only | Command arrives | Servo moves | **~6-43 ms** | PWM period + servo mechanics |

The key difference from a two-tier architecture: reflexive responses no longer include the 200-500 ms LLM inference latency, bringing them within human baseline ranges (see Section 1.1).

---

## 7. Bandwidth Analysis

With audio/video removed from the controller's data path, UART bandwidth is dedicated to actuator commands and telemetry.

### 6.1 Inbound Streams (SBC to Controller)

| Stream | Payload Size | Rate | Bandwidth | Notes |
|---|---|---|---|---|
| Actuator command (single) | ~9 bytes | Up to 1 kHz per actuator | Up to 36 KB/s (4 act.) | Position target per policy step |
| Coordinated command (group) | ~(4 + 9 x N) bytes | Up to 1 kHz | Up to 40 KB/s (N=4) | All actuators as atomic group |
| System commands | ~4-8 bytes | Rare | Negligible | E-stop, config |

Wire overhead per message: 9-byte header + 2-byte CRC + COBS (~1%) + 1-byte delimiter = ~13 bytes overhead.

### 6.2 Outbound Streams (Controller to SBC)

| Stream | Payload Size | Rate | Bandwidth | Notes |
|---|---|---|---|---|
| Actuator telemetry (standard) | ~(4 + 12 x N) bytes, N=4 | 100-1000 Hz | 5-52 KB/s | Single position sample per actuator |
| Actuator telemetry (oversampled) | ~(4 + (8+2S) x N) bytes | 100 Hz | ~12 KB/s (S=10,N=4) | Multiple position samples per actuator |
| System telemetry | ~16 bytes | 1-5 Hz | ~0.08 KB/s | State, faults, health |
| Acks | ~4 bytes | Per inbound command | 0.5-4 KB/s | One per received command |

### 6.3 UART Bandwidth Fit

| Baud Rate | Effective Throughput | Inbound (1 kHz cmds) | Outbound (100 Hz oversampled telem) | Combined | Utilization |
|---|---|---|---|---|---|
| 115,200 | ~9.2 KB/s | ~40 KB/s | ~12 KB/s | ~52 KB/s | **>100% -- impossible** |
| 230,400 | ~18.4 KB/s | ~40 KB/s | ~12 KB/s | ~52 KB/s | **>100% -- impossible** |
| 460,800 | ~36.9 KB/s | ~40 KB/s | ~12 KB/s | ~52 KB/s | **>100% -- tight** |
| 921,600 | ~73.7 KB/s | ~40 KB/s | ~12 KB/s | ~52 KB/s | **71% -- comfortable** |

**At 1 kHz command rate with per-actuator commands:** The bandwidth requirement is dominated by the high command rate. Strategies to fit within lower baud rates:

1. **Coordinated commands:** Send all 4 actuators in a single message at 1 kHz instead of 4 separate messages. Payload: ~40 bytes + 13 overhead = ~53 bytes per message at 1 kHz = ~53 KB/s. Still requires 921,600 baud.
2. **Reduced command rate with controller-side interpolation:** If the motor skill policy sends at 200-500 Hz instead of 1 kHz, the controller's 1 kHz interpolation smooths the motion. At 200 Hz: ~53 bytes x 200 = ~10.6 KB/s inbound. Combined with oversampled telemetry: ~22.6 KB/s. Fits at 460,800 baud (61% utilization).
3. **Compact binary commands:** Reduce per-actuator command payload (e.g., 16-bit position instead of float). This is a wire protocol change.

**Recommendation for P1:** 921,600 baud for full 1 kHz command rate, or 460,800 baud with 200-500 Hz command rate (controller interpolation bridges the gap to 1 kHz execution). Both are within the STM32G071RB USART capability.

---

## 8. Oversampled Sensor Telemetry

### 7.1 The Synchronization Problem

The architecture defines clock synchronization strategies (architecture.md Section 6) to correlate controller-side sensor measurements with SBC-side audio/video timestamps. Both GPIO-based (~1 us precision) and protocol-based (~1 ms precision) approaches require dedicated synchronization infrastructure: sync messages, offset state, drift tracking, and failure recovery.

### 7.2 The Oversampling Approach

Instead of synchronizing clocks, the controller **oversamples its position sensors** and sends multiple samples per telemetry report. The host timestamps report arrival and uses the known sample spacing to reconstruct each sample's temporal position.

```
Controller side:                        Host side:

  ADC samples at 10 kHz (every 100 us)   Receives telemetry report
  Every 10 ms, send telemetry report      containing 10 position samples
  containing last 10 samples per channel  per channel

  Report N:                               Host receives report at T_host
    sample[0] = oldest                      -> sample[9] taken ~0 ms ago
    sample[1] = oldest + 100 us             -> sample[8] taken ~1 ms ago
    ...                                     -> ...
    sample[9] = newest                      -> sample[0] taken ~9 ms ago

  Spacing is exactly 100 us              Host can interpolate to find
  (timer-triggered, crystal-accurate)     position at any arbitrary time
                                          in the window
```

The host needs only:
1. The arrival time (its own clock -- trivial).
2. The sample count and spacing (a configuration constant).
3. The UART transmission delay (bounded, measurable).

### 7.3 Comparison: Clock Sync vs. Oversampled Telemetry

| Aspect | GPIO Clock Sync | Protocol Clock Sync | Oversampled Telemetry |
|---|---|---|---|
| **Hardware required** | Dedicated GPIO + timer input capture | None | None (uses existing ADC) |
| **Extra wiring** | 1 wire per controller | None | None |
| **Firmware modules** | clock_sync.c, pal_capture.c | SoftSync handlers | ADC DMA at elevated rate |
| **Protocol messages** | TimeSyncPulse + TimeSyncReport | SoftSyncRequest/Response | None new -- samples ride in telemetry |
| **Host complexity** | GPIO pulse generation, offset/drift tracking | NTP-style offset/averaging/drift | Timestamp on arrival, subtract spacing |
| **Precision** | ~1 us | ~0.5-2 ms | ~0.5-2 ms + interpolation |
| **Drift handling** | Periodic re-sync pulses | Periodic re-sync exchanges | Not needed -- each report is self-contained |
| **Failure modes** | Missed pulse -> stale offset | Lost response -> stale offset | Lost report -> gap, next report independent |
| **Multi-controller** | 1 GPIO per controller | 1 sync session per controller | No per-controller sync needed |
| **Bandwidth cost** | ~20 bytes/s | ~20 bytes/s | ~40-200 extra bytes/s |
| **Stateful?** | Yes | Yes | **No** |

### 7.4 Oversampling Ratios and Bandwidth Cost

| Ratio | Samples/Report | Extra Bytes/Report (4 ch) | Extra BW at 100 Hz | Temporal Resolution |
|---|---|---|---|---|
| 1x (none) | 1 | 0 | 0 | 10 ms (report interval) |
| 2x | 2 | 16 | 1.6 KB/s | 5 ms |
| 4x | 4 | 32 | 3.2 KB/s | 2.5 ms |
| 10x | 10 | 80 | 8.0 KB/s | 1 ms |

For P1 with hobby servos (mechanical response ~5-20 ms), 2-4x oversampling is sufficient. 10x provides maximum temporal resolution at modest bandwidth cost.

**Controller RAM cost:** 4 channels x 10 samples x 2 bytes = 80 bytes (~0.2% of 36 KB RAM).

### 7.5 When Clock Sync Is Still Necessary

Oversampled telemetry is sufficient for P1's needs: correlating actuator position with audio/video (~1-5 ms precision), stall detection, telemetry display, and proprioceptive input to motor skill policies.

Clock sync (especially GPIO-based) may be needed in future for:
- **Sub-millisecond event correlation** (<100 us between controller and SBC events).
- **Hardware-triggered actions** at precise SBC-clock times.
- **Multi-controller coordination** requiring a common time base.

None of these apply to P1 (single controller, 4 hobby servos).

### 7.6 P1 Recommendation

Use oversampled telemetry at 2-4x with host-side timestamping as the default synchronization method. Defer clock sync modules from P1 firmware scope. Retain clock sync in the platform architecture as a future capability.

**Telemetry message format extension:**

```
Current (per actuator):
  actuator_id     (1 byte)
  status          (1 byte)
  current_pos     (4 bytes, float)    <-- single sample
  target_pos      (4 bytes, float)
  queue_depth     (1 byte)
  fault_code      (1 byte)
  Total: 12 bytes

Oversampled (per actuator):
  actuator_id     (1 byte)
  status          (1 byte)
  sample_count    (1 byte)            <-- number of position samples (1-10)
  target_pos      (4 bytes, float)
  queue_depth     (1 byte)
  fault_code      (1 byte)
  samples[]       (2 bytes x sample_count, uint16 raw ADC)
  Total: 8 + 2*N bytes (N = sample_count)
```

The sample_count and spacing are established during version handshake or telemetry configuration.

---

## 9. Eye Movement Control Loop

The eye movement control loop has the tightest performance requirements due to natural-looking saccades.

### 8.1 Saccade Dynamics

- Saccades (eye jumps): 20-200 ms duration, fast onset, slight overshoot, settle.
- Smooth pursuit (tracking): continuous, ~30-100 deg/s max.
- Fixation with micro-saccades: hold position with very small random perturbations.

### 8.2 Saccade Trajectory (Generated on SBC)

The motor skill policy generates a saccade as a sequence of position targets:

```
For a saccade from position p0 to p1:

  duration = f(|p1 - p0|)     // larger saccades are faster proportionally
                               // typical: 20 ms for 5 deg, 80 ms for 40 deg

  At each policy step (1 ms):

  t=0 ms:   target = p0 + 0.15 * (p1 - p0)     fast onset
  t=5 ms:   target = p0 + 0.55 * (p1 - p0)
  t=10 ms:  target = p0 + 0.90 * (p1 - p0)
  t=15 ms:  target = p0 + 1.05 * (p1 - p0)     overshoot
  t=20 ms:  target = p0 + 1.02 * (p1 - p0)     settling
  t=25 ms:  target = p1                          final position

  Each target sent as ActuatorCommand (OVERRIDE mode).
  Controller interpolates linearly at 1 kHz between successive targets.
```

Because the trajectory is computed on the SBC, changing it (different overshoot, different timing, a learned policy) requires no firmware change.

### 8.3 Coordinated Eye Movement

Both eye axes (H and V) move as a coordinated pair for diagonal saccades, using the coordinated motion mechanism (CTRL-R9 / MSG_COORDINATED_CMD):

```
CoordinatedCommand {
  actuator[0] = { id=EYE_H, pos=0.7, speed=max, mode=OVERRIDE }
  actuator[1] = { id=EYE_V, pos=0.4, speed=max, mode=OVERRIDE }
}

Both axes begin interpolation toward new targets on the same tick.
```

---

## 10. Neck Movement Control Loop

### 9.1 Neck Motion Dynamics

- Head turns toward a sound or person: 300-500 ms.
- Slow tracking: continuous, ~10-30 deg/s.
- Idle drift (micro-movements): 1-3 deg amplitude, ~0.5-2 deg/s.
- Eye-head coordination: eyes lead, head follows.

### 9.2 Eye-Head Coordination

```
Time -->

  Eyes:  --------/\------- (saccade: fast, overshoot, settle)
                /  \
  Head:  ------/----\----- (smooth turn: slower, follows eyes)
              /      \
  Net gaze:  same final direction, eyes arrive ~100-200 ms before head
```

The motor skill policy generates this coordination by sending eye commands immediately and head commands with a slight delay and slower profile. The controller doesn't know about the relationship -- it executes the commands it receives.

### 9.3 Parameter Comparison

| Parameter | Eyes | Neck |
|---|---|---|
| Typical speed | 100-500 deg/s (saccade) | 10-60 deg/s |
| Trajectory profile | Saccade (asymmetric, overshoot) | Minimum-jerk (smooth) |
| Command rate from SBC | 1 kHz | 1 kHz |
| Controller interpolation | Linear at 1 kHz | Linear at 1 kHz |

---

## 11. Controller Timing Budget

### 10.1 Per-Tick Budget (1 ms = 64,000 cycles at 64 MHz)

| Task | Cycles | Time | Notes |
|---|---|---|---|
| UART RX poll + framing | ~500-2000 | 8-31 us | Depends on bytes available |
| Protocol dispatch | ~200-500 | 3-8 us | Header parse, handler call |
| Actuator command processing | ~100-300 | 1.5-5 us | Validate, enqueue |
| Interpolation (4 actuators, linear) | ~80 | ~1.2 us | Linear interp (trajectory on SBC) |
| PWM CCR write (4 channels) | ~40 | ~0.6 us | Direct register write |
| ADC read (4 channels via DMA) | ~4 | ~0.06 us | DMA fills buffer, CPU reads |
| Sync manager tick | ~50-100 | 0.8-1.5 us | Timeout check |
| System manager tick | ~100-200 | 1.5-3 us | State, CBIT, LED |
| Telemetry tick (when due) | ~500-2000 | 8-31 us | Build + send (oversampled) |
| UART TX (DMA) | ~200-1000 | 3-15 us | Telemetry frame transmission |
| **Total (typical)** | **~1800-3000** | **~28-47 us** | |
| **Total (worst case)** | **~5000-7000** | **~78-109 us** | |

**Margin:** Worst case uses ~11% of the 1 ms budget at 64 MHz. Significant headroom for future expansion (more actuators, more complex processing).

---

## 12. STM32 MCU Comparison (Future Migration)

The STM32G071RB is selected for P1. This table documents the migration path for future prototypes with more actuators or on-controller computation.

| Parameter | STM32L031K6 (dev board) | STM32G071RB (P1) | STM32G474RE (P2 candidate) | STM32H743VI (P3+ candidate) |
|---|---|---|---|---|
| **Core** | Cortex-M0+ | Cortex-M0+ | Cortex-M4F | Cortex-M7 |
| **Max clock** | 32 MHz | 64 MHz | 170 MHz | 480 MHz |
| **Flash** | 32 KB | 128 KB | 512 KB | 2 MB |
| **RAM** | 8 KB | 36 KB | 128 KB | 1 MB |
| **FPU** | None | None | Single-precision | Single + double |
| **Timer PWM channels** | 4 practical | 12+ | 20+ | 30+ |
| **ADC** | 1x 12-bit, 10 ch | 1x 12-bit, 16 ch | 5x 12-bit, 42 ch | 3x 16-bit, 20 ch |
| **USART** | 1 + LPUART | 4 + LPUART | 5 + LPUART | 4 USART + 4 UART |
| **USB** | None | None | USB 2.0 FS | USB 2.0 HS (OTG) |
| **CAN** | None | None | 3x FDCAN | 2x FDCAN |
| **Servo capacity** | 4 (at limit) | 12+ | 20+ | 30+ |
| **On-controller policy** | Impossible | Impossible | Feasible (small MLP) | Practical |
| **Price (NUCLEO)** | ~$12 | ~$12 | ~$18 | ~$28 |

### 11.1 Migration Path

The firmware's PAL design (CTRL-R1) isolates hardware-specific code. Migrating MCUs requires updating only PAL modules:

| PAL Module | What Changes | Effort |
|---|---|---|
| pal_transport_uart.c | USART instance, pins, DMA channels | Low |
| pal_pwm.c | Timer instances, channel mapping, clock config | Low |
| pal_gpio.c | Pin mapping, clock enable | Trivial |
| pal_tick.c | SysTick prescaler | Trivial |
| pal_adc.c (new for P1) | ADC + DMA for position feedback | New module |

Everything above the PAL (actuator manager, protocol, framing, telemetry, system manager) requires **zero changes** when migrating MCUs.

---

## 13. Design Notes

### 12.1 Why Audio/Video Bypass the Controller

The controller is a Cortex-M0+ with no USB, no CSI, and limited UART bandwidth. Routing audio (~32-192 KB/s) or video (~5-50 Mbit/s) through the controller is impractical and unnecessary. The Jetson Orin Nano has dedicated high-bandwidth interfaces (USB 3.2, CSI-2) designed for media. Keeping media on the SBC also means the AI system can process raw audio/video frames without serialization overhead or protocol translation. The UART link is reserved for the low-bandwidth, latency-sensitive actuator command/telemetry traffic it was designed for.

### 12.2 Why Oversampled Telemetry for P1

Clock distribution solves the problem by establishing and maintaining a mapping between two clocks. This requires a sync protocol, state management, drift compensation, failure recovery, and per-controller instances. Oversampled telemetry eliminates all of this by making each telemetry report self-contained. There is no offset to maintain, no drift to track, no sync protocol, and no failure state. A report arriving after a 5-second communication dropout is just as usable as one during normal operation. The bandwidth cost (40-200 extra bytes/s) is trivial compared to the complexity saved. For P1's needs (~1-5 ms temporal precision for correlating servo position with audio/video), oversampling is the simpler and sufficient approach.

### 12.3 Why STM32G071RB Over STM32L031K6

The L031K6 can technically run P1 but is at its limit: 32 KB Flash leaves little room for growth, 8 KB RAM constrains oversampling buffers, and 4 PWM channels are exactly consumed with zero spare. The G071RB costs the same, uses the same Cortex-M0+ core (trivial port), and provides 4x Flash, 4.5x RAM, 3x timer channels, and a second I2C/SPI bus. It also comes in LQFP-64 (vs. LQFP-32), eliminating pin conflicts entirely.

### 12.4 Sensorimotor Tier at 500 Hz

The sensorimotor tier runs at 500 Hz on the SBC, sending position targets every 2 ms. The controller's 1 kHz execution loop interpolates linearly between these targets, producing smooth continuous motion. The 500 Hz rate provides 10+ trajectory steps for a 20 ms saccade (smooth motion) and sub-2 ms reaction capability for reflexive responses. It is achievable for scripted trajectory generators (trivial compute), reactive controllers (simple state machines), and small learned policies (MLP inference on Jetson GPU/CPU is sub-millisecond). The rate was chosen as a balance between trajectory smoothness and UART bandwidth (see Section 7.3) -- 1 kHz is possible at 921,600 baud but 500 Hz fits comfortably at 460,800 baud.
