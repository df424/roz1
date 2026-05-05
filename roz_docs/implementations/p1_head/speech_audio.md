# Speech Audio Output (P1) -- Architecture Options

This document scopes the P1 head’s **speaker output** path: how TTS audio becomes natural, loud speech from a physical speaker, and how playback timing stays coherent with jaw motion and the platform architecture (`roz_server` + `roz_control`).

## 1. Key Answer

Yes: TTS can output PCM and drive any Linux audio device, including a **COTS USB sound card/DAC** (USB Audio Class) or an **I2S**-connected amp/codec.

The real design problems are: SPL (loudness), latency/jitter, duplex audio (AEC), enclosure acoustics, and power/EMI.

## 2. Constraints From P1 + Platform

- P1: media is SBC-owned; speaker is not routed through the MCU.
- Jaw sync: audio playback and jaw envelope need a stable time relationship (chunk-level is fine; per-sample is not required).
- Platform: `roz_control` runs a 1 kHz control policy loop and must never block on audio I/O.

Platform fit note: even if playback is local to the SBC, it’s still useful if `roz_control` exposes a stable “audio stream” API (open/push/close + timestamps) whose backend can be local ALSA now and “stream to controller” in other topologies later.

## 3. Output Targets (Engineering)

- Quality: 24 kHz or 48 kHz PCM s16 (s24 optional).
- Loudness: design for ~90 dB SPL @ 1 m peak capability (then limit in software).
- Responsiveness: streaming TTS with small buffers; fast stop on interruption.
- Robustness: no dropouts during servo activity; no audible idle hiss.

## 4. Data Rates (Sanity)

| Format | Bytes/sec |
|---|---:|
| 24 kHz, 16-bit, mono | 48,000 |
| 48 kHz, 16-bit, mono | 96,000 |
| 48 kHz, 16-bit, stereo | 192,000 |

Bandwidth is not the limiter. Latency/jitter and analog power are.

## 5. Hardware Options (Pick One)

### Option 1: USB DAC (UAC) + Analog-In Class-D Amp (Baseline)

Block:

```
SBC (USB UAC) -> USB DAC/codec -> analog line -> class-D amp -> speaker
```

Pros: easiest Linux bring-up; swap DACs freely; amp power is a separate choice.

Requirements:
- stable ALSA device (UAC1/UAC2), supports chosen sample rate
- software limiter/compressor to prevent clipping and protect speaker

Rough BOM:
- USB DAC / USB audio adapter: $10-50
- class-D amp module (5-20 W): $8-25
- 2-3" full-range driver: $10-40

### Option 2: SBC I2S -> Digital-Input Class-D Amp -> Speaker

Block:

```
SBC I2S -> I2S class-D amp (optionally DSP/limiter) -> speaker
```

Pros: low and stable latency; fewer boxes/cables; can be integrated on a small PCB near the speaker.

Requirements:
- Jetson audio bring-up (pin mux / device-tree / ALSA)
- EMI containment (class-D + servo wiring coexistence)

Rough BOM:
- I2S class-D amp: $2-30 (power/features drive this)
- speaker: $10-40
- power filtering/regulation: $5-15

### Option 3: SBC I2S -> Audio Codec (DAC/ADC) + Amp (Best Duplex/AEC Story)

Block:

```
SBC I2S <-> codec (DAC+ADC) -> amp -> speaker
                 ^
                 +-- mic(s)
```

Pros: best path to robust AEC (shared clock + clean playback reference) and a “real product” audio subsystem.

Costs/risks: highest mixed-signal/bring-up complexity; usually implies a custom PCB.

Rough BOM:
- codec + passives: $3-15
- amp stage: $5-25
- speaker: $10-40

Non-goal for P1: Bluetooth speakers (latency/jitter, poor interruption control).

## 6. Software Ownership (Playback + Timing)

Do not run playback in the 1 kHz thread. Two workable models:

1. `roz_server` plays audio (Python): fastest to prototype, but timing is easier to break.
2. `roz_control` plays audio (C/C++): recommended for stable timestamps + coherent jaw sync + duplex AEC.

If `roz_control` owns playback, its API should mirror stream semantics:
- `audio_open(sample_rate, channels, fmt, chunk_period_ms)`
- `audio_push(pcm_chunk, t0)` (or it returns a monotonic playback timeline)
- `audio_close()` / `audio_stop()`

## 7. Jaw Sync (Practical)

Chunk-level sync is enough if done consistently:
- chunk period: 10-20 ms (or 20 ms if simpler)
- envelope: RMS/peak with attack/release smoothing
- `roz_control` consumes envelope events and generates jaw targets at 1 kHz (interpolated)

## 8. Power / PCB Notes

SPL back-of-the-envelope:

```
SPL_peak_1m ~= sensitivity_dB_1W1m + 10*log10(P_watts)
```

If you build a P1 audio PCB, it should include: power filtering, amp rail regulation, I2S (or USB routing), speaker connector, and amp enable/mute control.

## 9. Recommendation For P1

1. Start with **Option 1 (USB DAC + class-D amp)** to validate voice + enclosure + loudness + interruption behavior quickly.
2. If you need tighter timing or cleaner integration, move to **Option 2 (I2S amp)**.
3. If duplex hearing while speaking is central, plan a path to **Option 3 (codec)** and have `roz_control` own both playback and capture.

