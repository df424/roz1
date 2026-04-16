# Speech Audio Output (P1) -- Architecture Options

This document is a design exploration for the P1 head’s **audio output** path: how TTS audio becomes loud, natural-sounding speech from a physical speaker, and how that output stays coherent with jaw motion and the broader platform architecture.

It is intentionally option-oriented (not a single decision). The goal is to make tradeoffs explicit so we can pick a path and build it without revisiting basics.

## 1. Key Answer: Can TTS Drive a COTS Sound Card?

Yes. Any TTS system (LLM-based or not) ultimately produces PCM samples (or a decodeable audio bitstream). On Linux, you can play that audio to:
- a **USB Audio Class** device (COTS USB sound card / USB DAC),
- an onboard codec (if the carrier board has one),
- or an **I2S** audio device (DAC/codec/class-D amp) exposed as an ALSA PCM device.

The practical questions are not “can it interface”, but:
- latency and jitter (jaw sync, barge-in),
- loudness (SPL at the listener),
- acoustic feedback/echo cancellation,
- mechanical integration (speaker placement/enclosure),
- and power/EMI.

## 2. Constraints From P1 + Platform Docs

This audio output design has to satisfy (or at least not violate) the existing doc set:

- P1: jaw-audio synchronization and “media bypass” to SBC:
  - `P1-R4` (jaw envelope tracking) and `P1-R9` (SBC drives speaker via I2S or USB audio).
- Interaction considerations: short chunking and interruption:
  - Audio chunk loop is typically **20-50 ms** granularity for responsiveness and barge-in.
- Platform architecture: control loop at 1 kHz in `roz_control`, with `roz_server` hosting AI:
  - `roz_control` should not block its 1 kHz loop on audio I/O.
- Wire protocol: stream semantics exist, but P1 may bypass the MCU for speaker output:
  - `wire_protocol.md` defines StreamOpen/StreamData/StreamClose for audio; that’s useful as an *API shape* even if the physical speaker is directly on the SBC.

Platform fit note:
- Platform AI requirements currently describe speech as “stream audio via `roz_control`”, and (in some places) assume protocol-level synchronization concepts for jaw motion.
- For P1 (speaker directly on SBC), the clean way to stay consistent is: `roz_control` exposes an **audio streaming API** that looks like the wire-protocol stream model, but its backend can be either:
  - local SBC audio playback (USB/I2S), or
  - streaming over the controller link to a controller-driven speaker (future topology).

## 3. Output Targets (Design Requirements)

These are proposed concrete targets for P1 audio output. Treat them as engineering targets (not formal requirements) unless promoted.

**Audio quality**
- Sample rates: 24 kHz or 48 kHz
- Format: PCM s16 (minimum), s24 preferred if convenient
- Low noise: avoid audible hiss at idle in a quiet room

**Loudness**
- Target: “conversationally loud” at 0.5-1.0 m without distortion (roughly: comfortably audible over normal room noise).
- Stretch: brief higher SPL for effect (laugh, exclamation) without clipping or speaker damage.
- Practical engineering target: pick an amp/speaker combination capable of ~90 dB SPL at 1 m on peaks (then limit in software). In a head form factor, this usually implies **5-20 W** into a small full-range driver with decent sensitivity.

**Latency + coherence**
- Start speaking quickly: small initial buffering (streaming TTS).
- Jaw-audio coherence: maintain predictable latency and a stable mapping between “audio scheduled to speaker” and “jaw envelope timebase”.
- Interruption: stop audio output quickly (barge-in / e-stop), and stop jaw motion within the existing P1 expectations.

**Robustness**
- Must survive servo current spikes and EMI without audio dropouts.
- Must not starve the 1 kHz control loop.

## 4. Bandwidth / Data Rates (Sanity)

PCM payload rates (not including overhead):

| Format | Rate | Bytes/sec | Notes |
|---|---:|---:|---|
| 24 kHz, 16-bit, mono | 24k * 2 | 48,000 | Often sufficient for speech |
| 48 kHz, 16-bit, mono | 48k * 2 | 96,000 | Common default |
| 48 kHz, 16-bit, stereo | 48k * 4 | 192,000 | Stereo not required for P1 speech |
| 48 kHz, 24-bit, stereo | 48k * 6 | 288,000 | Higher quality, more bandwidth |

These are tiny on USB/I2S and irrelevant compared to camera data. The constraints are latency/jitter and analog power, not bandwidth.

## 5. Architecture Options (Hardware + Software)

### Option A: USB DAC / USB Sound Card + External Class-D Amplifier (Recommended Baseline)

**Block diagram**

```
--------------------+     USB (UAC)     +-----------------+   line-level   +------------------+   speaker wires   +---------+
| Jetson / SBC       |------------------->| USB DAC / codec |--------------->| Class-D amp PCB  |----------------->| Speaker |
| (roz_server/ai)    |                   | (COTS)           |                | (COTS or custom) |                  +---------+
--------------------+                   +-----------------+                +------------------+
```

**Why this is attractive**
- Lowest integration risk on Linux: USB Audio Class “just works”.
- Easy to swap DACs for quality/driver reasons without respinning a PCB.
- Lets you choose amplifier power to hit the SPL you want.

**Key requirements**
- USB audio device must support the chosen sample rate (24/48 kHz) and sample format.
- Amp must be sized for your acoustic target (typ. 5-20 W class-D is plenty for a head).
- Provide hard limiter/soft compressor in software to prevent clipping and protect the speaker.

**BOM estimate (rough, per head)**
- USB DAC / USB audio adapter: $10-50 (wide range; pick for noise floor + reliability)
- Class-D amplifier module (analog input): $8-25
- Speaker driver (2-3" full range): $10-40
- Wiring/connectors/fasteners: $5-15

**PCB implications**
- You may not need a custom PCB initially. A “distribution board” can still be useful:
  - power input + filtering
  - amp mounting
  - speaker connector
  - optional mic preamp / routing for future AEC work

**Protocols/interfaces**
- USB Audio Class (UAC1/UAC2), ALSA device
- Software stack: ALSA directly, or PipeWire, but keep a “low-latency exclusive” mode available.

**Risks**
- USB latency can be slightly higher than I2S (still usually fine for 50 ms jaw alignment if you use consistent buffering).
- USB device quality varies wildly; choose deliberately.

---

### Option B: SBC I2S -> I2S Class-D Amp (MAX98357A-class) -> Speaker (Fast, Simple, Lower Power)

**Block diagram**

```
Jetson I2S (BCLK/LRCLK/DIN) ---> I2S class-D amp ---> Speaker
```

**Why this is attractive**
- Very low and stable latency (no USB stack).
- Minimal parts count; easy to embed on a small PCB near the speaker.

**The catch**
- Most “simple I2S amp” breakouts are in the ~1-3 W class at 5 V. That can sound good, but may not hit “effect” loudness depending on speaker choice and enclosure.

**BOM estimate**
- I2S class-D amp (breakout or chip): $2-10
- Speaker: $10-40
- Power regulation/filtering: $3-10

**PCB implications**
- Strong candidate for a custom PCB integrated into the head:
  - I2S input header from SBC
  - class-D amp with good layout + EMI containment
  - speaker connector
  - bulk capacitance and LC filtering on the amp rail

**Interfaces**
- I2S (3.3 V logic): BCLK, LRCLK, DIN (MCLK optional depending on part)
- Power: typically 5 V (verify per amp)

**Risks**
- Jetson pin mux / device-tree configuration work.
- Class-D EMI in a head full of sensitive sensors and servo wiring.

---

### Option E: SBC I2S -> High-Power Digital-Input Class-D Amp (I2S + DSP) -> Speaker (Low Latency, High SPL)

This is the “I2S, but actually loud” option: use a digital-input amplifier that accepts I2S directly and can deliver materially more power than MAX98357A-class parts.

**Block diagram**

```
Jetson I2S ---> I2S class-D amp w/ DSP (I2C-configured) ---> Speaker
```

**Why this is attractive**
- Low and stable latency (I2S path, no USB).
- Enough power headroom for realistic loudness.
- DSP can implement limiter/EQ in hardware (optional).

**BOM estimate (very rough)**
- Digital-input class-D amp IC/module: $8-30
- Speaker: $10-40
- Power (often 12 V recommended for higher power): filtering + buck(s): $5-15

**PCB implications**
- Custom PCB is strongly implied:
  - power stage layout matters
  - EMI filtering becomes a first-class requirement
  - I2C control routing (for amp configuration) if needed

**Risks**
- More integration effort than Option A/B (parts, layout, bring-up).
- Higher EMI and power-transient risk if the power architecture isn’t solid.

---

### Option C: SBC I2S -> Audio Codec (DAC/ADC) + Amp (Unified Audio I/O, Better AEC Story)

This is the “real product” audio approach: use a codec that supports both playback and capture so you can do echo cancellation with a clean reference and shared clocking.

**Block diagram**

```
Jetson I2S <----> Codec (DAC/ADC) ----> Amp ----> Speaker
                      ^
                      |
                   Microphone
```

**Why this is attractive**
- Best path to robust duplex audio:
  - AEC (echo cancellation) works best when playback and capture share clocking and you have the exact playback reference.
- Lets you choose mic topology later (single mic, stereo, array) without changing the fundamental architecture.

**BOM estimate**
- Codec: $2-10 (plus passives)
- Amp stage: $5-20 depending on power
- Speaker + mic(s): $15-60
- PCB: more complex (analog routing, ground partitioning)

**PCB implications**
- This pushes you toward a real mixed-signal PCB:
  - careful ground strategy
  - analog anti-aliasing and output filtering
  - shielding/placement to avoid servo EMI

**Risks**
- Highest integration complexity (mixed-signal PCB + Jetson audio configuration).
- Most likely to cause schedule slip if tackled too early.

---

### Option D: Powered Speaker Module (Fast Demo, Least Control)

Use an off-the-shelf powered speaker (USB-powered, line-in, or Bluetooth).

**Pros**
- Fastest path to “it talks and sounds decent”.

**Cons**
- Latency control is poor (especially Bluetooth).
- Harder to integrate with AEC and jaw sync.
- Industrial design may be awkward inside the bust.

## 6. Where the Audio Pipeline Should Live (Software Ownership)

Given the platform architecture (`roz_server` hosts AI; `roz_control` owns 1 kHz control):

1. **Audio playback must not run in the 1 kHz thread.**
   - Playback is a separate real-time-ish stream (24k/48k Hz). It should live in its own thread/process with bounded buffering.

2. Two viable ownership models:

**Model 1: `roz_server` owns audio playback (Python)**
- `roz_ai` / TTS produces PCM chunks.
- `roz_server` writes PCM to ALSA/PipeWire.
- `roz_server` also computes an amplitude envelope and pushes “jaw envelope events” into `roz_control` (which applies them at 1 kHz).

Risk: Python jitter and audio-stack complexity can create occasional timing weirdness unless carefully engineered.

**Model 2: `roz_control` owns audio playback (C/C++)**
- `roz_control` exposes a C API like `roz_control_audio_stream_open/push/close`.
- Internally it runs a dedicated audio thread with ALSA, and it provides a monotonic “audio timeline” to the 1 kHz policy loop for jaw sync.
- `roz_server`/`roz_ai` is a producer of PCM chunks only.

This is more work up front, but it keeps the timing-critical path out of Python and makes jaw sync less fragile.

Either way: compute jaw envelope from the exact PCM that will be played, and treat “audio scheduled time” as a first-class clock domain.

Implementation note: it is useful if `roz_control`’s audio API mirrors the existing wire-protocol stream concepts:
- open (format + sample rate + chunk period)
- push chunk
- close
Even if P1’s speaker is local to the SBC, reusing that “stream” abstraction reduces architectural drift.

## 7. Jaw Sync Approach (Practical)

Jaw does not need per-sample control. Chunk-level (20-50 ms) is usually enough if you:
- generate a smooth envelope (RMS/peak over a sliding window),
- apply attack/release time constants (prevents chatter),
- and provide a predictable delay between “envelope time” and “audio at speaker”.

Recommended shape:
- Audio chunk size: 20 ms
- Envelope update rate: 50 Hz (per chunk) or 100 Hz (10 ms windows)
- `roz_control` consumes envelope and produces jaw targets at 1 kHz (interpolated)

## 8. Power / SPL Reality Check

The biggest determinant of “sounds good and feels real” is usually the **speaker + enclosure**, not the DAC.

Guidance:
- Choose a speaker with decent sensitivity and a real enclosure volume. A leaky 3D-printed cavity will sound thin.
- Size amplifier power with headroom (avoid clipping), then limit in software.
- Class-D amps are efficient, but they inject switching noise; plan filtering and layout.

SPL back-of-the-envelope (for choosing amp power):

```
SPL_peak_at_1m ~= speaker_sensitivity_dB_at_1W1m + 10*log10(P_watts)
```

Example: an 86 dB @ 1W/1m driver with 10 W peak power is ~96 dB @ 1m (before enclosure and distortion effects).

## 9. PCB Notes (If We Build A P1 Audio Board)

If we decide to do a custom PCB (typically Option B/C/E, or as a “distribution board” for Option A), the board wants:
- Power input (robot supply) with bulk capacitance and EMI filtering
- A protected amp rail (buck or LDO depending on amp choice)
- The audio interface connector:
  - I2S header (BCLK/LRCLK/DIN/GND, optional MCLK)
  - and/or USB header (or just a physical USB cable path)
- Speaker connector (locking)
- Optional: test points for audio clocks and amp enable/mute
- Optional: mic front-end (if pursuing Option C and AEC seriously)

## 10. Recommendation For P1 (To Move Fast Without Painting Us Into a Corner)

1. Start with **Option A (USB DAC + class-D amp)** to prove:
   - TTS voice quality
   - SPL target in the actual head enclosure
   - jaw sync and interruption behavior
   - barge-in behavior (mic + speaker at once)

2. If latency/jitter is a real problem, migrate to **Option B (I2S amp)** or **Option C (codec)** once the mechanical layout is stable.

3. Software: strongly consider **`roz_control` owning audio playback** if jaw sync and barge-in are central to the product experience.
