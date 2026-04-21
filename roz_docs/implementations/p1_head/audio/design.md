# P1 Audio Subsystem -- Design

## 1. Overview

This document defines the hardware selections, signal chain, software architecture, and integration details for the P1 head's audio subsystem. It describes how the audio requirements ([requirements.md](requirements.md)) are realized using COTS components connected to the Jetson Orin Nano SBC.

### 1.1 Architecture Summary

The audio subsystem uses two independent USB audio devices on the Jetson Orin Nano: one for speaker output and one for stereo microphone input. A hardware mute line from the STM32 controller provides e-stop speaker kill.

```
Jetson Orin Nano (4x USB 3.2 Type-A)
    │
    ├── USB-A ──► USB DAC (UAC2) ──► 3.5mm ──► Class D Amp ──► Speaker (mouth)
    │                                              │
    │                              P1 5V rail ─────┘
    │                              STM32 GPIO ──► amp SHDN pin
    │
    └── USB-A ──► USB Stereo Mic Interface (UAC) ──► L mic (left ear)
                                                  ──► R mic (right ear)
```

Two of the Jetson's four USB-A ports are consumed. Two remain available for development (keyboard, debug adapter, etc.).

### 1.2 Reference Documents

- [P1 audio requirements](requirements.md) (P1A-R*)
- [P1 head requirements](../requirements.md) (P1-R*)
- [P1 head design](../design.md)
- [Speech audio research](../speech_audio.md)
- [Hearing audio research](../ears_audio.md)

---

## 2. Speaker Output Chain

### 2.1 USB DAC

**Role:** Converts PCM audio from the Jetson to an analog line-level signal.

**Selection criteria:**
- UAC2 compliant (driverless on Linux, lower latency than UAC1)
- 48 kHz / 16-bit minimum (P1A-R2)
- SNR > 90 dB (amplified idle noise must remain inaudible)
- 3.5mm analog output
- Compact form factor (fits inside or mounts near the head)

**Form factor:** USB-C DAC dongle (CX31993, CS43131, or equivalent chipset) with a USB-A to USB-C adapter or cable. These are typically 30-50mm long and weigh a few grams.

**Validation:** Before committing, test the specific dongle on JetPack to confirm: ALSA enumeration (`aplay -l`), 48 kHz playback, and output noise floor measurement.

### 2.2 Class D Amplifier

**Role:** Amplifies the DAC's analog output to drive the speaker.

**Selected part:** MAX98306-based breakout board (e.g., Adafruit product #987).

| Parameter | Value |
|---|---|
| Output power | 3.7W per channel into 4Ω (mono: use one channel) |
| Supply voltage | 3.3-5.5V (powered from P1 5V rail) |
| Topology | Filterless Class D |
| Shutdown pin | Yes (active-low SHDN) |
| Board size | ~27 x 25 mm |
| Input | Analog, 3.5mm or solder pads |

**Why this part:**
- Shutdown pin is guaranteed and documented (P1A-R5)
- 3.7W into 4Ω with an 85 dB/W/m driver produces ~91 dB SPL peak at 1 m (P1A-R1)
- Filterless Class D simplifies wiring (no output inductor/capacitor)
- 5V operation avoids routing the 12V rail to the audio subsystem
- Small enough to mount near the speaker inside the head

**Power:** From the P1 5V servo rail. Peak current draw at 3.7W into 4Ω is ~1A. The 5V rail is sized for servo stall current (P1-R8b), so the amp's draw is a small fraction of the rail's capacity. A 10 uF + 100 nF bypass capacitor at the amp's power input filters servo transients.

### 2.3 Speaker Driver

**Role:** Acoustic transducer firing forward through the mouth opening.

**Selection criteria:**
- 2" (50mm) full-range driver (fits behind the mouth opening of a human-sized head)
- 4Ω impedance (matches Class D amp for maximum power delivery)
- Sensitivity: 82-86 dB/W/m (typical for this size)
- Frequency response covering speech fundamentals and formants (150 Hz - 8 kHz)

The driver is mounted to the fixed skull structure, not the jaw. The jaw mechanism moves below and in front of the speaker.

### 2.4 Enclosure Acoustics

The 3D-printed skull cavity acts as a sealed enclosure behind the speaker driver.

- **Cavity volume:** ~2-3 liters (human-sized skull interior, shared with other components). Sufficient for sealed-box bass extension down to ~150-200 Hz, which covers male speech fundamentals.
- **Damping:** Polyfill or acoustic foam loosely packed in the cavity behind the speaker. Damps standing waves and reduces PLA/PETG shell resonance.
- **Speaker mounting:** The driver is mechanically fastened to the skull shell with a gasket seal. A 3D-printed baffle ring or mounting bracket positions the driver behind the mouth opening.

**Jaw-speaker acoustic interaction:** The jaw mechanism moves in front of the speaker. When the jaw opens, the mouth aperture widens, allowing more direct sound radiation. When the jaw is at rest (partially closed), some sound is partially occluded but not blocked (the jaw should never fully seal the mouth). This creates a natural coupling between jaw position and perceived loudness. Validate empirically -- if attenuation with jaw closed is excessive, adjust the speaker's vertical position or mouth geometry.

---

## 3. Microphone Input Chain

### 3.1 USB Stereo Microphone Interface

**Role:** Captures stereo audio from two mic capsules placed at the left and right ear positions.

**Selection criteria:**
- UAC compliant (driverless on Linux)
- Stereo (2-channel) mic input
- 48 kHz / 16-bit capture (P1A-R6)
- Low self-noise (< -60 dBFS noise floor)
- Compact form factor
- No proprietary DSP / AGC that can't be disabled (raw PCM access required)

**Form factor:** Compact USB audio interface with two mic/line inputs and no built-in speakers or unnecessary features. Alternatively, a small USB codec breakout board with two electret mic preamps.

### 3.2 Microphone Capsules

**Role:** Transduce acoustic sound at the ear positions.

Two electret condenser or MEMS mic capsules, wired to the USB stereo interface:
- Placed at left and right ear positions on the bust (~15 cm spacing for DOA)
- Recessed behind acoustically transparent mesh or cloth
- Small ports (5-8 mm diameter) in the shell at each ear position
- Foam gasket between capsule and shell for vibration isolation from servos

**Orientation:** Capsules face outward (toward the sound source). The mesh cover protects against dust and provides visual concealment.

### 3.3 Mic Placement Priorities

1. **Away from speaker blast path:** Ear positions are ~90 degrees from the forward-firing mouth speaker, providing natural geometric attenuation of the robot's own voice.
2. **Vibration isolation:** Foam gaskets decouple capsules from the PLA shell, reducing servo noise coupling.
3. **Consistent geometry:** L/R spacing and orientation must be repeatable across units for DOA calibration.
4. **Cable routing:** Mic cables route from ear positions through the skull interior to the USB interface mounted near the Jetson or controller PCB.

---

## 4. AEC and Duplex Architecture

### 4.1 Problem

When the robot speaks, its own voice is picked up by the microphones. Without cancellation, VAD triggers on the robot's own speech, DOA points at the mouth, and barge-in detection fails (P1A-R7, P1A-R10).

### 4.2 Approach

Software acoustic echo cancellation (AEC) using the playback signal as a reference. The playback thread provides a copy of the outgoing PCM to the AEC module, which subtracts the estimated echo from the mic signal before passing it to VAD/DOA.

```
┌──────────────────────────────────────────────────────┐
│  roz_control / roz_server                            │
│                                                      │
│  playback thread ──► USB DAC (speaker)               │
│       │                                              │
│       │ reference copy                               │
│       ▼                                              │
│  AEC module ◄──── capture thread ◄──── USB mic       │
│       │                                              │
│       ▼                                              │
│  cleaned signal ──► VAD ──► barge-in event           │
│                 ──► DOA ──► azimuth estimate          │
│                 ──► roz_ai (speech recognition)       │
└──────────────────────────────────────────────────────┘
```

### 4.3 Clock Drift

The speaker USB device and mic USB device have independent clocks. Typical drift is 1-50 ppm (~0.5-2.5 samples/second at 48 kHz). This is a well-understood problem -- AEC libraries (webrtc AEC3, speexdsp) include internal drift compensation. No special hardware is required.

### 4.4 Software Selection

**Recommended:** webrtc AEC3. It is:
- Battle-tested (Chrome, Android, WebRTC clients)
- Written in C++ (integrates with roz_control)
- Handles clock drift between capture and playback devices
- Includes noise suppression and gain control as optional stages

---

## 5. Hardware Mute and E-Stop

### 5.1 Hardware Path

The MAX98306 SHDN (shutdown) pin is active-low. Driving it low immediately disables the amplifier output (< 1 ms, P1A-R5). The pin is wired to an STM32 GPIO on the controller PCB.

```
STM32 GPIO (output, active-low) ──── wire ────► MAX98306 SHDN pin
                                                (active-low: LOW = mute)
```

Normal operation: GPIO held HIGH (amp enabled). Software controls audio by sending or not sending PCM to the USB DAC.

E-stop (P1-R13d): Controller receives e-stop command, asserts GPIO LOW in the same ISR that halts servo motion. Speaker is hardware-silenced within 1 ms, independent of SBC software state.

### 5.2 GPIO Allocation

This adds one GPIO output to the STM32's allocation (see [P1 head design](../design.md) Section 5.2). The pin should be configured with an internal pull-down so the amp defaults to muted on controller reset or power-up, and only enables once the controller firmware initializes and explicitly asserts HIGH.

---

## 6. Power

### 6.1 Amp Power

The MAX98306 is powered from the P1 5V servo rail (P1-R8). Bypass capacitors (10 uF + 100 nF) at the amp's VDD input filter servo switching transients.

Peak current: ~1A at 3.7W into 4Ω. Average current during speech: ~200-400 mA. This is a small fraction of the 5V rail's capacity.

### 6.2 USB Bus Power

Both USB audio devices (DAC and mic interface) are bus-powered from the Jetson's USB ports. Typical draw: < 100 mA each. No external power required.

### 6.3 Mic Power

Electret mic capsules require bias voltage (typically 1-3V), supplied by the USB mic interface's preamp circuit. No external bias supply needed.

---

## 7. Signal Routing Inside the Head

### 7.1 Cable Summary

| Cable | From | To | Type | Length (est.) |
|---|---|---|---|---|
| USB (speaker) | Jetson USB-A | USB DAC dongle | USB-A to USB-C | ~15 cm |
| Analog audio | USB DAC 3.5mm out | Amp input pads | 3.5mm to bare wire or header | ~10 cm |
| Speaker wire | Amp output pads | Speaker driver | 2-conductor, 22-26 AWG | ~5-10 cm |
| USB (mic) | Jetson USB-A | USB mic interface | USB-A cable | ~15 cm |
| Mic L cable | USB mic interface | Left ear capsule | Shielded 2-conductor | ~15 cm |
| Mic R cable | USB mic interface | Right ear capsule | Shielded 2-conductor | ~15 cm |
| Mute control | STM32 GPIO | Amp SHDN pin | Single wire + ground | ~10 cm |
| Amp power | P1 5V rail | Amp VDD/GND | 2-conductor, 22 AWG | ~10 cm |

### 7.2 EMI Considerations

- **Amp near speaker:** Minimize the analog wire run between the amp output and the speaker driver. Mount the amp board within 5-10 cm of the speaker. This keeps the high-current Class D switching signals short.
- **Mic cables shielded:** Use shielded cable for mic runs to reject interference from the Class D amp and servo bus.
- **Separation:** Route mic cables and speaker/amp wires on opposite sides of the head cavity where possible. Keep analog audio cables away from the SPI and servo bus wiring.

---

## 8. Integration Points

### 8.1 Jaw-Audio Synchronization

The speaker output chain provides the audio signal that drives jaw synchronization (P1-R3). The jaw sync policy in roz_control extracts the amplitude envelope from the same PCM chunks sent to the USB DAC (see [P1 head design](../design.md) Section 7). The USB DAC's output latency (~5-10 ms I2S buffer) means the jaw command consistently leads the acoustic output, which is within the 50 ms alignment target (P1-R3a).

### 8.2 Barge-In Flow

1. Human speaks while robot is speaking.
2. Mic captures mixed signal (human + robot echo).
3. AEC removes robot echo using playback reference.
4. VAD detects human speech on cleaned signal.
5. Barge-in event sent to roz_ai within 200 ms (P1A-R7).
6. roz_ai stops TTS, roz_control halts jaw motion (P1-R3b).

### 8.3 Controller Integration

The only controller-side integration is the amp SHDN GPIO (Section 5). All audio I/O is SBC-owned. The controller firmware needs:
- One GPIO output pin allocated for amp enable/mute.
- E-stop handler updated to assert the mute GPIO alongside servo halt.
- Default pin state: LOW (muted) until firmware explicitly enables.

---

## 9. BOM Summary

| Part | Description | Power | Est. Cost |
|---|---|---|---|
| USB DAC dongle | UAC2, compact, SNR > 90 dB | USB bus | $15-35 |
| Class D amp board | MAX98306 breakout, 3.7W, SHDN pin | 5V rail | ~$9 |
| Speaker driver | 2" full-range, 4Ω, ~85 dB/W/m | -- | $10-20 |
| USB stereo mic interface | UAC, 2-ch capture, compact | USB bus | $15-40 |
| 2x mic capsules | Electret condenser, wired | Bias from interface | $5-15 |
| Acoustic damping | Polyfill or foam for skull cavity | -- | ~$5 |
| Cables and adapters | USB, audio, speaker wire, mute wire | -- | $5-10 |
| Bypass capacitors | 10 uF + 100 nF at amp VDD | -- | ~$1 |
| **Total** | | | **~$65-135** |

---

## 10. Open Items

1. **USB DAC validation:** Test a specific UAC2 dongle on JetPack -- confirm ALSA enumeration, 48 kHz playback, output noise floor.
2. **USB mic interface selection:** Identify a compact UAC stereo input device with raw PCM access (no proprietary DSP).
3. **Speaker driver selection:** Confirm 2" driver fits the mouth opening geometry from the 3D model.
4. **AEC integration:** Scope webrtc AEC3 build and integration with roz_control's audio pipeline.
5. **Amp SHDN GPIO pin assignment:** Allocate a specific STM32 GPIO for the mute line in the controller PCB design.
6. **Acoustic validation:** Test speaker output with jaw at various positions to characterize attenuation.
