# Sensorimotor Architecture

## 1. Overview

This document describes an aspirational three-tier control architecture for ROZ that extends the platform's current two-tier model (LLM + control policy) with an intermediate sensorimotor tier. The goal is to produce lifelike, socially believable robot behavior -- particularly for embodied platforms like humanoid heads and upper bodies where reflexive responses and ambient motion are critical to the impression of presence.

This architecture is not required for all ROZ implementations. The current two-tier model (architecture.md Section 7) is sufficient for functional robots. The sensorimotor tier is an evolution for implementations that prioritize behavioral realism and embodied interaction quality.

### 1.1 Motivation

The two-tier model (LLM at 1-5 Hz, control policy at 1000 Hz) has a gap: the control policy translates LLM directives into actuator commands, but it does not act autonomously in response to perceptual events. All motor behavior originates from the LLM.

This creates a realism problem. Human reflexive responses are fast:

| Response | Human Latency | LLM Latency (typical) |
|---|---|---|
| Auditory startle / orienting | 80-250 ms | 200-500 ms (first token) |
| Saccade to visual stimulus | 150-250 ms | 200-500 ms |
| Smooth pursuit onset | 100-130 ms | N/A (continuous) |
| Head turn toward sound | 200-400 ms | 300-700 ms (directive + policy) |

For responses in the 80-250 ms range, LLM inference latency alone exceeds the deadline. A robot that waits for the LLM before reacting to a sudden sound or a face appearing will feel sluggish and robotic, regardless of how good the LLM's response eventually is.

### 1.2 Reference Documents

- [System architecture](architecture.md) -- current two-tier model
- [AI system requirements](../ai/requirements.md) (AI-R13, motor skills)
- [Controller requirements](../controller/requirements.md) (MCU-R6, interpolation)
- [Embodied interaction considerations](embodied_interaction_considerations.md)

---

## 2. Three-Tier Model

### 2.1 Tier Definitions

| Tier | Location | Rate | Role |
|---|---|---|---|
| Cognitive | SBC (GPU) | 1-5 Hz | LLM inference, semantic reasoning, directive generation |
| Sensorimotor | SBC (CPU) | 1 kHz | Reactive control, trajectory generation, priority arbitration, sensor-motor coordination |
| Servo execution | Controller MCU | 1 kHz+ (ideally 10 kHz) | Interpolation, servo bus I/O, limit enforcement, fault monitoring |

The **cognitive tier** is unchanged from the current architecture -- the LLM observes, reasons, and produces semantic motor directives.

The **sensorimotor tier** is the new element. It sits between the cognitive tier and the servo execution tier, consuming both LLM directives and perceptual events (audio onset, face detection, motion vectors). It produces actuator commands at 1 kHz. Critically, it can act on perceptual events without waiting for the LLM -- this is what enables reflexive responses within human latency ranges.

The **servo execution tier** replaces the simple "controller receives commands and drives actuators" model. It receives 1 kHz setpoints from the sensorimotor tier and executes them at the controller's native rate (1 kHz minimum per MCU-R23, ideally 10 kHz for inter-setpoint interpolation -- see controller design note 3.5). It also performs limit enforcement, stall detection, and fault monitoring independently of the SBC.

### 2.2 Relationship to the Two-Tier Model

The three-tier model is a strict superset of the two-tier model:

```
Two-tier (current):
  LLM (1-5 Hz) ──► Control Policy (1 kHz) ──► Controller ──► Actuators

Three-tier (sensorimotor):
  LLM (1-5 Hz) ──► Sensorimotor Tier (1 kHz) ──► Controller (10 kHz) ──► Actuators
                        ▲
                        │
                   Perceptual Events
                   (audio, vision, proprioception)
```

The control policy from the two-tier model maps to the sensorimotor tier. The difference is that the sensorimotor tier also has:
- Direct perceptual event input (not just LLM directives)
- Reactive controllers that produce motor output autonomously
- Priority arbitration between reactive and deliberate behaviors
- Sensor-motor coordination patterns (e.g., eye-head coordination)

An implementation can adopt the sensorimotor architecture incrementally -- start with the two-tier model and add reactive controllers as needed.

---

## 3. Reactive Control

### 3.1 Concept

A reactive controller is a lightweight function that maps a perceptual event directly to a motor response, bypassing the LLM. Reactive controllers are:
- **Fast:** ~1-5 ms latency contribution (the rest of the budget is perception pipeline latency)
- **Simple:** stateless or minimal state, no inference
- **Interruptible:** any higher-priority behavior preempts them immediately
- **Suppressible:** the LLM can disable specific reactive controllers when they would interfere with deliberate behavior

### 3.2 Examples

| Reactive Behavior | Trigger | Motor Response | Typical Total Latency |
|---|---|---|---|
| Audio orienting | Sound onset above threshold | Gaze saccade toward estimated source direction | ~100-250 ms |
| Face tracking | Face detected in camera frame | Smooth pursuit gaze tracking | ~100-150 ms |
| Startle | Sudden loud sound | Head retraction + blink | ~80-120 ms |
| Motion saccade | Visual motion in periphery | Gaze shift toward motion | ~150-250 ms |
| Barge-in | Speech detected during robot speech | Stop jaw motion, halt TTS | ~50-100 ms |

These latencies are achievable because the perception pipeline (VAD, face detection, audio energy estimation) runs continuously and produces events asynchronously. The reactive controller responds within 1-2 ticks of receiving the event.

### 3.3 Priority Arbitration

When multiple behaviors (reactive and deliberate) demand the same actuator simultaneously, a priority scheme resolves conflicts:

| Priority | Source | Behavior |
|---|---|---|
| CRITICAL | Safety, startle | Overrides everything |
| DIRECTIVE | LLM directive | Overrides reactive behaviors |
| HIGH | Fast reactive (audio orient) | Overrides medium/low |
| MEDIUM | Sustained reactive (face track) | Overrides low |
| LOW | Ambient reactive (motion saccade) | Yields to all |
| IDLE | Idle animation | Active only when nothing else is |

The cognitive tier can also issue suppression commands to temporarily disable specific reactive controllers (e.g., suppress audio orienting while the robot is deliberately looking at something else).

---

## 4. Sensor-Motor Coordination Patterns

### 4.1 Eye-Head Coordination

For gaze shifts, eyes and head must coordinate. The natural human pattern is "eyes lead, head follows":

1. Eyes saccade toward the target immediately
2. Head begins a slower rotation ~50 ms later
3. As the head moves, eyes counter-rotate to maintain gaze on target (vestibulo-ocular reflex analog)
4. Final state: gaze is split between eye position and head position

This coordination happens in the sensorimotor tier, not the cognitive tier. The LLM issues "look at bearing X" -- the sensorimotor tier decides how to split the gaze across eyes and neck.

### 4.2 Jaw-Audio Synchronization

During speech, jaw motion tracks the audio amplitude envelope. The TTS pipeline produces audio chunks; the sensorimotor tier extracts the amplitude envelope and generates jaw position commands synchronized with the audio output. The LLM decides *what* to say; the sensorimotor tier handles *how the jaw moves while saying it*.

### 4.3 Idle Animation

When no behavior is active, the sensorimotor tier generates ambient motion (gaze drift, micro-saccades, small head movements) that prevents the robot from appearing frozen. Idle animation is the lowest priority behavior and is preempted immediately by any stimulus.

---

## 5. Implementation Considerations

### 5.1 Sensorimotor Tier Runtime

The sensorimotor tier runs at 1 kHz on the SBC. At this rate, deterministic timing requires attention to:
- Real-time scheduling (SCHED_FIFO or equivalent)
- CPU pinning to avoid scheduling interference from other workloads
- Avoiding blocking I/O, garbage collection pauses, or priority inversion in the hot loop

For prototyping, a best-effort implementation (e.g., Python with a high-priority thread) may be sufficient to validate the architecture. For production, the sensorimotor tier's hot loop should run in compiled code -- potentially as a set of policies within roz_control's 1 kHz loop.

### 5.2 Perception Pipeline Interface

The sensorimotor tier consumes perceptual events produced by the SBC's perception pipeline. Events are delivered asynchronously (e.g., via a lock-free queue) and processed on each sensorimotor tick. The perception pipeline runs at its own rate (e.g., audio VAD at audio chunk rate, face detection at camera frame rate).

### 5.3 Incremental Adoption

An implementation can adopt sensorimotor capabilities incrementally:

1. **Baseline:** Two-tier model. roz_control executes LLM directives via scripted/learned policies. No reactive behaviors.
2. **Add jaw sync:** Jaw-audio synchronization as a reactive behavior in the control policy. Minimal complexity, high impact on realism.
3. **Add idle animation:** Ambient motion generator running when no LLM directive is active.
4. **Add gaze reactives:** Audio orienting and face tracking as reactive controllers with priority arbitration.
5. **Full sensorimotor:** Eye-head coordination, startle, full priority arbitration, LLM suppression commands.

Each step adds realism without requiring the full architecture to be in place.

---

## 6. Design Notes

### 6.1 Why Not Run Reactive Controllers on the MCU?

The reactive controllers need perceptual events (face detection results, audio source direction estimates) that are produced by the SBC's perception pipeline. Streaming these to the MCU would add latency and consume controller link bandwidth. The SBC has ample CPU for a 1 kHz control loop alongside the LLM and perception pipelines.

### 6.2 Relationship to AI-R13 (Motor Skills)

AI-R13 defines motor skills as the interface between the LLM and the control policy. The sensorimotor architecture does not change this interface -- the LLM still issues semantic motor skill directives. The sensorimotor tier adds a second source of motor commands (reactive controllers) that coexist with LLM directives via priority arbitration. From the LLM's perspective, the motor skill interface is unchanged.

### 6.3 Terminology

The term "sensorimotor" is borrowed from neuroscience, where the sensorimotor cortex integrates sensory input with motor output. In ROZ, the sensorimotor tier serves the analogous function: it sits between perception (sensory) and actuation (motor) and produces motor responses conditioned on sensory input, without requiring higher-level reasoning.
