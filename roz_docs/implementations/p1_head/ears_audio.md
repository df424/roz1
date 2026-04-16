# Hearing Microphones (P1) -- Architecture Options

This document is a design exploration for the P1 head’s **microphones (ears)**: how we capture audio in a way that supports natural interaction (good voice quality, low latency, barge-in while speaking), and how that input integrates with the larger platform architecture (`roz_server` + `roz_control`) and the speech output path (`speech_audio.md`).

It is intentionally option-oriented (not a single decision). The goal is to make tradeoffs explicit so we can pick a path and build it without revisiting basics.

## 1. Key Answer: Can We Use COTS Microphones?

Yes. On Linux (Jetson or dev machine), microphone capture can be implemented using:
- **USB Audio Class (UAC)** microphones (single, stereo, or arrays) exposed via ALSA.
- **I2S / PDM / TDM digital MEMS microphones** connected to the SBC’s audio interface.
- **Analog microphones** captured through a codec / USB audio interface (ADC).

The hard parts are not “can we get samples”, but:
- duplex audio (speaker + mic at once) and **echo cancellation (AEC)**,
- **direction of arrival (DOA)** estimation for orienting behavior,
- latency and jitter (for reflexive reactions and interruption),
- mechanical placement and noise (servo noise, enclosure resonance),
- and power/EMI.

## 2. Constraints From P1 + Platform Docs

Microphone design touches multiple subsystems:

- P1 realism requirements: reflexive orient-to-sound depends on fast audio events and (ideally) DOA:
  - P1 requirements include reactive behaviors driven by audio onset and estimated direction.
- Embodied interaction considerations call out:
  - VAD confidence and noise floor,
  - sound direction feeding attention/gaze,
  - echo cancellation when listening while speaking,
  - barge-in as a high-priority interrupt path.
- Platform architecture:
  - `roz_control` runs a 1 kHz control policy loop and should not block on audio I/O.
  - `roz_server` hosts AI and serves UI; it should have visibility into audio health/levels.
- AI requirements:
  - audio is a primary perceptual input (continuous capture, chunking, VAD metadata).

Platform fit note:
- Even if the physical mic hardware is directly connected to the SBC, it is still useful for `roz_control` to expose a stable **audio input API** (frames + timestamps + metadata). The backend can be:
  - local SBC capture (USB/I2S), or
  - controller-captured audio streamed over the transport (future topology).

## 3. Input Targets (Engineering Requirements)

These are proposed engineering targets for P1 audio input. Treat them as targets (not formal requirements) unless promoted.

**Capture quality**
- Sample rate: 16 kHz minimum; 48 kHz preferred for processing headroom (beamforming/AEC).
- Format: PCM s16 minimum.
- Low self-noise: no audible hiss when the room is quiet and no one is speaking.

**Latency + chunking**
- Deliver audio frames to perception in predictable windows (typ. 10-20 ms).
- Support “fast path” audio events (onset/energy spikes) without waiting for long buffers.

**Duplex interaction**
- Must support listening while speaking:
  - AEC is strongly recommended to make barge-in and VAD credible.

**Direction / attention**
- Support an estimated sound direction (even coarse) to drive gaze orienting:
  - minimum: left/right discrimination (stereo),
  - better: full azimuth estimate (array).

**Robustness**
- Must remain stable under servo noise, vibration, and speaker output.
- Must not starve the 1 kHz control loop.

## 4. Bandwidth / Data Rates (Sanity)

PCM capture payload rates:

| Format | Bytes/sec | Notes |
|---|---:|---|
| 16 kHz, 16-bit, mono | 32,000 | Speech-only baseline |
| 48 kHz, 16-bit, mono | 96,000 | Common processing default |
| 48 kHz, 16-bit, stereo | 192,000 | Enough for basic DOA (ITD/ILD) |
| 48 kHz, 16-bit, 4-ch | 384,000 | Small array |
| 48 kHz, 16-bit, 6-ch | 576,000 | Typical COTS arrays |

Again: bandwidth is not the issue on USB/I2S. The constraints are latency, jitter, and acoustic robustness.

## 5. Architecture Options (Hardware + Software)

### Option A: USB Mono Microphone (UAC) (Fastest Bring-Up, No DOA)

**Block diagram**

```
Mic (USB UAC) ---> SBC (ALSA capture) ---> VAD / onset detect ---> AI + control
```

**Why this is attractive**
- Zero custom hardware.
- Lowest software bring-up effort.

**Limitations**
- No usable sound direction estimate, so “orient to sound source” becomes guessy.
- Duplex audio quality depends heavily on AEC strategy and mic placement.

**BOM estimate**
- USB microphone: $10-80 (quality varies widely)

**Risks**
- Many cheap USB mics have poor noise floor and aggressive AGC that hurts VAD and AEC.

---

### Option B: USB Stereo “Ears” (2 Mics) (Recommended Simple DOA)

Use two matched microphones placed at left/right “ear” positions and capture them as a 2-channel USB audio device.

**Block diagram**

```
L/R mics ---> USB stereo codec (UAC) ---> SBC ---> DOA (GCC-PHAT) + VAD + AEC ---> control + AI
```

**Why this is attractive**
- Minimal complexity while enabling coarse DOA (left/right, and sometimes rough azimuth).
- Lets you do your own DOA and beamforming in software (no vendor black box).

**Key requirements**
- Microphone spacing and placement must be stable and known (calibration).
- Provide per-channel gain matching or calibrate it.

**BOM estimate (rough)**
- USB stereo audio interface / codec board: $10-40
- 2x mic elements (MEMS or electret modules): $2-20
- cabling/mounts/mesh: $5-15

**PCB implications**
- You can do this as COTS modules + wiring for P1.
- A small “ears board” later could host the two MEMS mics + USB codec.

**Risks**
- Without AEC, DOA will be dominated by the robot’s own speech while speaking.

---

### Option C: USB Microphone Array Module (4-6 Mics) (Fast Feature Ramp)

Use a COTS USB mic array that exposes multichannel PCM (and sometimes built-in DOA/AEC).

**Block diagram**

```
USB mic array (UAC) ---> SBC ---> beamforming/AEC/VAD (software or module DSP) ---> control + AI
```

**Why this is attractive**
- Fastest path to “it hears well” and “it knows where sound is”.
- Array geometry improves SNR and DOA robustness.

**BOM estimate**
- COTS USB mic array: ~$30-150 depending on quality/features

**Risks**
- Some modules rely on proprietary DSP / firmware.
- Multichannel USB capture increases software complexity (but not bandwidth).

---

### Option D: Digital MEMS Mic(s) via I2S/PDM (Low Latency, Custom Hardware)

Use one or more PDM/I2S digital MEMS microphones into the SBC audio interface.

**Block diagram**

```
MEMS mic(s) ---> I2S/PDM ---> SBC ALSA driver ---> VAD/onset/DOA/AEC ---> control + AI
```

**Why this is attractive**
- No USB stack; stable latency and clocking.
- Easy to build into a custom PCB form factor (“ears”).

**BOM estimate**
- MEMS mic: $1-4 each
- level shifting (if required) + passives: $1-5

**Risks**
- Jetson pin mux / device-tree / driver work.
- Board-level EMI/clock integrity issues if layout is sloppy.

---

### Option E: TDM Digital Mic Array (4-8ch) into SBC (Best “Product-Like” Path)

TDM lets multiple MEMS microphones share a clock and data line with deterministic channel mapping.

**Why this is attractive**
- Scales to arrays without USB.
- Strong foundation for robust beamforming + AEC.

**Costs/risks**
- Highest bring-up complexity on Jetson-class SBCs (routing + software enablement).
- Likely a custom PCB.

---

### Option F: Audio Codec (ADC) + Analog Mics (Unified Duplex Audio)

This pairs naturally with the “codec-based speaker” output option in `speech_audio.md`.

**Block diagram**

```
Analog mic(s) ---> codec ADC <---I2S---> SBC <---I2S---> codec DAC ---> amp ---> speaker
```

**Why this is attractive**
- Best AEC story (shared clocking, clean playback reference).
- Full control of mic preamp gain and filtering.

**BOM estimate**
- codec + passives: $3-15
- mic elements + preamp (if needed): $5-25
- custom PCB: likely

**Risks**
- Mixed-signal PCB + Jetson audio configuration complexity.

---

### Option G: Pro-Audio USB Interface + External Mic (Dev/Lab Only)

This is a debugging/measurement setup, not a production path.

**Pros**
- Excellent audio quality and gain control.

**Cons**
- Too large and awkward for a head enclosure.

## 6. Where the Microphone Pipeline Should Live (Software Ownership)

As with speech output, microphone capture must not run in the 1 kHz thread.

Two viable ownership models:

**Model 1: `roz_server` owns mic capture (Python)**
- Capture via ALSA/PipeWire in Python.
- Run VAD/onset/DOA in Python or a native extension.
- Push “audio events” into `roz_control` and feed audio chunks to the LLM.

Risk: Python scheduling and GC can create tail-latency spikes unless tightly controlled.

**Model 2: `roz_control` owns mic capture (C/C++) (Recommended)**
- `roz_control` runs a dedicated audio capture thread (ALSA) with a ring buffer.
- It emits:
  - raw audio frames (mono/stereo/multichannel) with timestamps,
  - metadata (VAD, energy, noise floor),
  - optional DOA estimates (azimuth + confidence).
- `roz_server` consumes frames/metadata and feeds the AI system; the 1 kHz policy loop consumes events/metadata only.

This model keeps the capture timing-critical path out of Python and makes AEC integration cleaner (especially if `roz_control` also owns playback).

## 7. Audio Processing Stack (What We Likely Need)

At minimum (P1):
- **VAD** with confidence + noise floor estimate.
- **Onset/energy detector** for reflexive attention shifts.

Recommended for duplex interaction:
- **AEC** (echo cancellation) using the playback reference.
- Optional noise suppression (servo noise, room noise).

Recommended for “ears” realism:
- **DOA estimation**:
  - stereo: GCC-PHAT style time-delay estimation,
  - array: SRP-PHAT/beamforming-based estimation.

Typical frame sizing:
- processing frame: 10 ms (48 kHz -> 480 samples per channel)
- event update: 10-20 ms
- AI chunking: 20-50 ms (matches the existing “chunk loop” assumptions)

## 8. Mechanical Placement (The “Ears” Problem)

The physical design dominates microphone performance:
- Put mics where they are not blasted directly by the speaker.
- Provide acoustic ports with mesh and foam to reduce wind/handling noise.
- Mount mics with vibration isolation (grommets/foam) to reduce servo vibration coupling.
- Keep symmetry if you want DOA to work without constant recalibration.

If you want DOA that feels believable, approximate human geometry helps:
- place left/right mics as far apart as feasible within the head (10-18 cm is typical in small busts).

## 9. PCB Notes (If We Build Ear Mic Hardware)

If we build a custom “ears” PCB (Option D/E/F), it likely wants:
- 2-6 MEMS mics in known geometry
- clean clock routing for PDM/I2S/TDM
- local LDO filtering for mic power (quiet rail)
- shield/ground strategy for servo + class-D EMI environment
- a connector strategy that doesn’t put fragile wires under strain (head movement)

## 10. Recommendation For P1

To move quickly while still enabling “orient to sound” and barge-in:

1. Start with **Option B (USB stereo ears)** or **Option C (USB mic array)**.
   - If you want maximum control and transparency: Option B (compute DOA in software).
   - If you want the fastest end-to-end demo: Option C (array), but choose a module that exposes raw multichannel PCM.

2. Plan on AEC early if the robot will listen while speaking. Without AEC, barge-in and DOA will be unreliable in realistic environments.

3. Prefer **`roz_control` owning capture** (and ideally playback) so timestamps and duplex processing are stable, while `roz_server` focuses on AI orchestration.
