# P1 Audio Subsystem -- Requirements

## 1. Overview

This document defines the requirements for the P1 head's audio subsystem: speaker output (speech) and microphone input (hearing). The audio subsystem enables the robot to speak aloud, synchronize jaw motion with speech, detect and localize human speech, and support barge-in (interrupting the robot while it is speaking).

### 1.1 Scope

These requirements cover the audio hardware and software pipeline on the SBC. They derive from the P1 head requirements ([requirements.md](../requirements.md)) and constrain the audio design ([design.md](design.md)). Media is SBC-owned (P1-R4); audio does not transit the embedded controller or controller link.

### 1.2 Reference Documents

- [P1 head requirements](../requirements.md) (P1-R*)
- [P1 audio design](design.md)
- [P1 head design](../design.md)
- [AI system requirements](../../../ai/requirements.md) (AI-R*)

---

## 2. Requirements

### 2.1 Speaker Output

**P1A-R1 - Speaker Output Level**: The speaker shall produce a peak SPL of at least 90 dB at 1 m on axis.

**P1A-R2 - Output Audio Format**: The output path shall support 48 kHz, 16-bit PCM, mono.

**P1A-R3 - Output Latency**: End-to-end latency from PCM buffer write to acoustic output shall be less than 20 ms.

**P1A-R4 - Output Cessation**: Audio output shall stop within 50 ms of a software stop command (derives from P1-R3b).

**P1A-R5 - Hardware Speaker Mute**: The amplifier shall have a hardware enable input controllable by the embedded controller. Assertion to silence shall complete within 1 ms (derives from P1-R13d).

### 2.2 Microphone Input

**P1A-R6 - Capture Audio Format**: The capture path shall support 48 kHz, 16-bit PCM, stereo (2 channels).

**P1A-R7 - Barge-In**: The system shall detect human speech during robot speech output and generate a barge-in event within 200 ms of speech onset.

**P1A-R8 - Direction of Arrival**: The system shall estimate the azimuth of detected speech with at least left/center/right discrimination.

**P1A-R9 - Mic Noise Immunity**: Microphone capture shall remain functional (VAD operational) during servo motion at idle amplitudes (P1-R2c).

### 2.3 Duplex Operation

**P1A-R10 - Simultaneous Capture and Playback**: The system shall capture microphone input while simultaneously playing speaker output without feedback-induced artifacts degrading speech detection.
