# Hearing Microphones (P1) -- Architecture Options

This document scopes the P1 head’s **microphones (ears)**: how we capture audio for natural interaction (good voice quality, low latency, barge-in while speaking), and how capture timing integrates with `roz_server` + `roz_control` and the speaker path (`speech_audio.md`).

## 1. Key Answer

Yes: we can use COTS microphones on Linux via **USB Audio Class (UAC)** or use **I2S/PDM/TDM** digital MEMS microphones. The difficult parts are duplex AEC, DOA estimation, and mechanical placement.

## 2. Constraints From P1 + Platform

- Reflexive behaviors benefit from fast audio onset detection and (ideally) a direction estimate.
- Duplex: if the robot listens while speaking, AEC is strongly recommended for usable VAD/DOA and barge-in.
- Platform: the 1 kHz control loop must not block on audio I/O; treat audio as its own threaded pipeline with timestamps.

Platform fit note: even with SBC-owned mics, a stable `roz_control` audio-input API (frames + timestamps + metadata) keeps the platform consistent across different robot topologies.

## 3. Input Targets (Engineering)

- Capture: 48 kHz PCM s16 preferred (16 kHz mono is the minimum viable speech baseline).
- Frame cadence: 10 ms processing frames are a good default (20 ms also workable).
- Metadata: VAD confidence + energy + noise floor; optional DOA azimuth + confidence.
- Robustness: stable under servo noise and speaker playback; no “cheap mic AGC” surprises.

## 4. Data Rates (Sanity)

| Format | Bytes/sec |
|---|---:|
| 16 kHz, 16-bit, mono | 32,000 |
| 48 kHz, 16-bit, stereo | 192,000 |
| 48 kHz, 16-bit, 6-ch | 576,000 |

Bandwidth is not the limiter; latency/jitter and acoustics are.

## 5. Hardware Options (Pick One)

### Option 1: USB Stereo “Ears” (2 Mics) (Recommended Baseline)

Block:

```
L/R mics -> USB stereo codec (UAC) -> SBC -> VAD/onset + DOA (software) + AEC
```

Pros: simple, transparent, enables coarse DOA; easy to prototype with COTS parts.

Rough BOM:
- USB stereo interface/codec: $10-40
- 2x mic elements/modules: $2-20

Key requirement: stable “ear” geometry and gain matching (or calibration).

### Option 2: USB Mic Array (4-6ch) (Fast Feature Ramp)

Block:

```
USB mic array (UAC) -> SBC -> beamforming/DOA + VAD + (AEC)
```

Pros: quickest path to strong DOA/SNR; arrays are more robust in real rooms.

Risks: avoid modules that hide everything behind proprietary DSP; prefer ones that expose raw multichannel PCM.

Rough BOM:
- USB mic array: ~$30-150

### Option 3: Codec / Digital MEMS (I2S/PDM/TDM) (Product Path)

Block (two common shapes):

```
MEMS (PDM/TDM) -> SBC audio IF -> software VAD/DOA/AEC
or
analog mics -> codec ADC <-> SBC I2S (shares clock with playback codec) -> AEC-friendly duplex
```

Pros: best integration and best AEC story when paired with the codec-based speaker option.

Costs/risks: Jetson audio bring-up + custom PCB + EMI/clock integrity.

Rough BOM:
- MEMS mic(s): $1-4 each, plus passives
- or codec + passives: $3-15 (plus mic front-end as needed)

## 6. Software Ownership (Capture + Duplex)

Do not run capture in the 1 kHz thread.

- Fast prototype: `roz_server` captures audio and produces events.
- Recommended: `roz_control` owns capture in a dedicated thread with a ring buffer and timestamps, and (ideally) also owns playback so AEC has a clean reference.

## 7. Processing Stack (Minimum Viable vs Good)

Minimum viable:
- VAD (+ confidence)
- onset/energy detector

For realistic duplex interaction:
- AEC using the playback reference (best when playback is also owned by `roz_control`)
- optional noise suppression (servo + room noise)

For “ears” attention:
- DOA estimate (stereo GCC-PHAT; arrays SRP-PHAT/beamforming)

## 8. Mechanical / PCB Notes

Placement dominates quality:
- keep mic ports out of the speaker’s direct blast path
- isolate from vibration
- maximize left/right spacing if you want DOA (10-18 cm helps)

If we build an “ears” PCB later: known geometry, quiet mic rail (LDO filtering), clean clock routing, and shield strategy for a servo + class-D environment.

## 9. Recommendation For P1

1. Start with **Option 1 (USB stereo ears)** to get DOA + VAD working with minimal integration risk.
2. If DOA robustness is a priority, jump to **Option 2 (USB array)** with raw multichannel access.
3. If duplex hearing while speaking becomes central, plan **Option 3 (codec/MEMS)** alongside the codec speaker option, and have `roz_control` own both capture and playback.

