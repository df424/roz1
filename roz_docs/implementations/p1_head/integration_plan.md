# P1 Head Integration Plan

This document tracks the phased integration of P1 head subsystems (audio
first, with neck / eyes / jaw to follow). It is the running checklist for
hardware bring-up and feature integration -- distinct from the platform
requirements (`controller/requirements.md`, `ai/requirements.md`) and the
hardware specification (`p1_head/audio/wiring.md` etc.).

Each phase is a milestone with an explicit completion bar. Items deferred
during a phase land in the **Backlog** section with a trigger condition
that brings them back into a future phase.

## Audio Subsystem

| # | Phase | Status | Bar |
|---|---|---|---|
| 1 | Hardware sanity (ALSA-level loopback) | Done 2026-04-29 | `arecord` -> `aplay` round-trips through Cubilux/Plugable chain |
| 2 | Python audio I/O via sounddevice | Done 2026-04-29 | `audio_bringup.py loopback` works end-to-end |
| 3 | Piper TTS one-shot | Done 2026-05-01 | `audio_bringup.py say "..."` produces speech via the speaker |
| 4 | Compose long-running demo app | Done 2026-05-01 | `audio_bringup.py interactive` keeps voice + devices warm; steady-state synth+resample ~250 ms (well under conversational tolerance) |
| 5 | Mic input refinement | Deferred | SNR + click rate quantified; breadboard replaced by `wiring.md` shielded layout |
| 6 | VAD + record-on-speech | Deferred | Capture starts/ends on energy + ZCR threshold per `ears_audio.md` Section 7 |

### Phase notes

**Phase 1** -- breadboarded chain works in both directions. Mic capture
is noisy and near-field-only -- expected for a raw electret on a cheap
USB adapter; see backlog **B1**.

**Phase 2** -- `audio_bringup.py` provides `list / record / play /
loopback` subcommands with sample-rate negotiation. USB devices opened
by PortAudio index use ALSA `hw:` directly and only allow native rates,
so the script probes and resamples through scipy when needed. Default
rate is 48 kHz (matches USB native and `ears_audio.md` preference).

**Phase 3** -- `say` subcommand uses Piper via `piper-tts` (onnxruntime
CPU inference, ~100-150 ms per short utterance after warmup). Cold
start is ~1-2 s for model load + ORT first-run graph optimization;
this is what Phase 4 amortizes.

**Phase 4** -- `interactive` subcommand wraps `say / record / play` in a
REPL that loads the voice once and keeps the audio devices open. Two
warmup costs are paid once per process: voice load (~1.4 s) and ORT
first-inference graph optimization (~900 ms on the first `say`).
Steady-state synth+resample is ~250 ms for short utterances --
dominated about evenly by Piper inference and the 22050->48000 scipy
polyphase resample. This is the topology `roz_server` will eventually
adopt; the bringup script is the prototype.

**Phase 5** -- not started. Trigger: clean capture is needed for VAD or
STT integration.

**Phase 6** -- not started. Depends on Phase 5.

## Backlog

Items deferred from the phase plan, with the trigger that pulls each one
back in.

| # | Item | Trigger |
|---|---|---|
| B1 | Mic input refinement: shielded cable per `wiring.md`, click root-cause, USB array eval (`ears_audio.md` Option 2) | Phase 5 |
| B2 | GPU acceleration for onnxruntime on Jetson (TensorRT EP, or NVIDIA `jetson-containers` builds) | When STT (Whisper) or LLM (Gemma) integration begins |
| B3 | TTS streaming + jaw envelope extraction (chunk-level RMS for jaw target generation) | When jaw actuator is in the loop |
| B4 | Promotion of `bringup/p1_head/audio/audio_bringup.py` into `roz_server` | When `roz_server` daemon design lands |

### B2 detail

GPU-accelerated ONNX inference on Jetson is non-trivial. The standard
`onnxruntime-gpu` wheel doesn't ship aarch64 GPU support; the working
paths are NVIDIA's `jetson-containers` builds or installing the
TensorRT execution provider against the JetPack stack. Pay-off is
large for Whisper and Gemma, both of which are compute-bound. Pay-off
is small for Piper, where inference latency on CPU is already
sub-200 ms after warmup -- the perceived 1-2 s "TTS lag" was cold
start (model load + ORT session init), not inference. Defer until the
AI phase begins.

## Other Subsystems

Empty for now. Sections will be added as integration work begins on:

- Neck yaw + position feedback
- Eye horizontal / vertical
- Jaw + jaw-audio sync
- Sensorimotor tier reactive controllers
- Controller link (UART) bring-up between SBC and STM32
