# ROZ AI System (roz_ai) - Requirements Document

## 1. Overview

This document defines the requirements for roz_ai, the compound AI system that serves as the robot's cognitive core. roz_ai runs on the SBC (NVIDIA Jetson Orin Nano) and implements a continuous perception-reasoning-action loop: it ingests raw audio, downsampled camera frames, and controller telemetry; reasons about the robot's environment and interactions using a multimodal LLM; and produces actions (speech, motion, expressions) that are executed through the host library.

### 1.1 Scope

This project covers the AI system running on the SBC: perception pipeline, core reasoning loop, action generation, text-to-speech, behavior management, and safety. It does **not** cover the wire protocol, controller firmware, or host library -- those are separate projects that roz_ai consumes via roz_host's Python API.

### 1.2 System Context

```
                          Audio (mic)
                          Camera frames
                          Telemetry
                               │
                               ▼
┌──────────────────────────────────────────────────────────┐
│                         roz_ai                           │
│                                                          │
│  ┌──────────────────┐    ┌────────────────────────────┐  │
│  │   Perception     │    │     Behavior Manager       │  │
│  │   Pipeline       │    │  (personality, context,     │  │
│  │                  │    │   conversation history,     │  │
│  │  audio──┐        │    │   state tracking)          │  │
│  │  video──┼──►prep │    └────────────┬───────────────┘  │
│  │  telem──┘        │                 │                  │
│  └────────┬─────────┘                 │                  │
│           │    ┌──────────────────┐    │                  │
│           │    │  Long-Term       │    │                  │
│           │◄───│  Memory          │    │                  │
│           │    │  (vector index,  │────┘                  │
│           │    │   retrieval)     │                       │
│           │    └──────┬───────────┘                       │
│           │           │ ▲                                │
│           ▼           │ │ idle extraction                │
│  ┌────────────────────┴─┴───────────────────────────┐    │
│  │              Core Reasoning (Gemma4)              │    │
│  │         multimodal LLM inference loop             │    │
│  │   audio + vision + text + memory + context        │    │
│  └──────────────────────┬───────────────────────────┘    │
│                         │                                │
│           ┌─────────────┼─────────────┐                  │
│           ▼             ▼             ▼                  │
│  ┌─────────────┐ ┌───────────┐ ┌──────────────┐         │
│  │   Action    │ │    TTS    │ │   Safety     │         │
│  │  Generation │ │  Pipeline │ │   Layer      │         │
│  │  (motion,   │ │  (text →  │ │  (filter,    │         │
│  │   express.) │ │   audio)  │ │   validate)  │         │
│  └──────┬──────┘ └─────┬─────┘ └──────────────┘         │
│         │              │                                 │
│  ┌──────┴──────────────┴────────────────────────────┐    │
│  │         Interaction Buffer (disk)                 │    │
│  │         (raw record of all interactions)          │    │
│  └──────────────────────────────────────────────────┘    │
│                                                          │
└─────────┼──────────────┼─────────────────────────────────┘
          │              │
          ▼              ▼
      roz_host       roz_host
     (actuator      (audio stream
      commands)      to speaker)
```

### 1.3 Reference Hardware

**Primary target:** NVIDIA Jetson Orin Nano Developer Kit (8GB RAM, 40 TOPS, 1024 CUDA cores, Ampere GPU).

The system shall be designed to run the largest feasible Gemma4 model on this hardware, using quantization as needed. If performance is insufficient, the hardware may be upgraded; the software architecture shall not be coupled to this specific board.

### 1.4 Reference Documents

- [System Architecture](../system/architecture.md) -- system context and project relationships.
- [Host Library Requirements](../host/requirements.md) -- the API through which roz_ai controls the robot.
- [Wire Protocol](../protocol/wire_protocol.md) -- the underlying protocol (for understanding telemetry and command semantics).

---

## 2. Requirements

### 2.1 Core AI Loop

**R1 - Continuous Operation**: The AI system shall run a continuous perception-reasoning-action loop for the full duration the robot is powered on. There is no wake word, trigger phrase, or activation gesture. The robot is always perceiving, always reasoning, and always capable of acting.

**R2 - Loop Structure**: Each iteration of the AI loop shall:
  - (a) Gather current perceptual input (audio, vision, telemetry).
  - (b) Construct a model context combining perceptual input with behavioral state (personality, conversation history, robot state).
  - (c) Invoke the core LLM for inference.
  - (d) Parse the model's output into actionable directives (speech, motion, expression, internal state updates).
  - (e) Execute actions through roz_host and update internal state.

**R3 - Responsiveness**: The AI loop shall be designed to minimize perceived latency between a stimulus (e.g., a person speaking) and the robot's response (e.g., beginning to reply). The system shall prioritize beginning a response quickly over waiting for complete input processing. Where the model and pipeline support it, streaming inference and incremental action execution shall be used.

**R4 - Graceful Degradation**: If any component of the perception pipeline is unavailable (e.g., camera disconnected, microphone failure, controller offline), the AI loop shall continue operating with the remaining inputs. The system shall report degraded perception to the behavior manager so the robot can acknowledge its limitations.

### 2.2 Perception Pipeline

**R5 - Audio Perception**: The system shall continuously capture audio from the robot's microphone via roz_host and present it to the core LLM.
  - (a) Audio shall be fed directly to the model in its native format without speech-to-text conversion. The multimodal LLM is the speech recognizer.
  - (b) The perception pipeline shall segment audio into chunks suitable for the model's input requirements (chunk size and overlap are implementation-defined based on the model's capabilities).
  - (c) The pipeline shall implement voice activity detection (VAD) to distinguish speech from silence and ambient noise. VAD output shall be available to the behavior manager to inform attention and turn-taking decisions.
  - (d) When no speech is detected, the system may reduce the frequency of audio-inclusive inference cycles to conserve compute, while maintaining visual and telemetry awareness.

**R6 - Visual Perception**: The system shall continuously receive camera frames from the robot's camera via roz_host and present them to the core LLM.
  - (a) Frames shall be downsampled to a resolution suitable for the model's vision input. The target resolution shall balance perceptual fidelity against inference cost.
  - (b) The pipeline shall feed frames to the model at a configurable rate (e.g., 1-5 fps). The rate may be adaptive -- increasing when visual activity is detected, decreasing during static scenes.
  - (c) The pipeline should implement basic scene change detection to prioritize feeding new frames when the visual environment changes, rather than at a fixed interval.

**R7 - Telemetry Perception**: The system shall receive controller telemetry via roz_host and incorporate it into the model's context.
  - (a) Telemetry shall be formatted as structured text describing the robot's physical state: actuator positions, active motions, fault conditions, system status.
  - (b) Telemetry updates shall be included in the model context at each reasoning cycle, giving the LLM awareness of the robot's current physical configuration.
  - (c) Significant state changes (fault, emergency stop, controller disconnect) shall be flagged as high-priority context for immediate model attention.

**R8 - Perceptual Fusion**: The perception pipeline shall present audio, visual, and telemetry inputs to the model as a coherent multimodal context within a single inference call. The model receives the full sensory picture, not isolated channels.

### 2.3 Core Reasoning

**R9 - Multimodal LLM**: The core reasoning engine shall be a multimodal large language model capable of processing audio, images, and text in a single inference call.
  - (a) The reference model is the Gemma4 family. The system shall run the largest Gemma4 variant that achieves acceptable inference latency on the target hardware.
  - (b) The model shall be loaded with quantization as needed to fit within the SBC's GPU memory while maintaining acceptable output quality.
  - (c) The system shall support swapping the model for a different variant or family without changes to the perception pipeline or action generation layers. The model is a pluggable component.

**R10 - Inference Runtime**: The system shall use an inference runtime suitable for on-device deployment on the Jetson platform.
  - (a) The runtime shall support GPU-accelerated inference on NVIDIA hardware.
  - (b) The runtime shall support quantized model formats (e.g., INT4, INT8, FP16).
  - (c) The runtime should support streaming token generation where the model produces output incrementally, enabling the action layer to begin executing before the full response is generated.

**R11 - Context Management**: The system shall manage the model's context window across inference cycles.
  - (a) The system shall maintain a rolling context that includes: system prompt (personality), recent conversation history, current perceptual input, robot state summary, and retrieved long-term memories (R35).
  - (b) When the context approaches the model's maximum length, the system shall summarize or evict older history to make room for new input, preserving the most relevant context.
  - (c) The system shall track token usage and ensure the context window is not exceeded, which would cause inference failure or truncation.
  - (d) The context budget shall be partitioned across components (system prompt, memory, conversation history, perceptual input) with configurable allocations to prevent any single component from consuming the entire window.

### 2.4 Action Generation

**R12 - Structured Output**: The core LLM shall produce structured output that the action generation layer can parse into discrete robot actions. The output format shall distinguish between:
  - (a) **Speech**: Text to be spoken by the robot.
  - (b) **Motion**: Named motion primitives or explicit actuator targets (e.g., "nod", "look left", "express surprise").
  - (c) **Expression**: Compound behaviors that combine motion and audio (e.g., "laugh" = jaw movement pattern + laugh audio).
  - (d) **Internal**: State updates, memory notes, or reasoning that does not produce external action.
  - (e) **Attention**: Directives about what to focus on visually (e.g., "look at the person speaking").

**R13 - Motion Primitives**: The action generation layer shall maintain a library of named motion primitives that map to sequences of actuator commands.
  - (a) Each primitive shall be defined as a parameterized sequence of actuator commands executable via roz_host.
  - (b) Primitives shall be composable -- multiple primitives may execute concurrently on different actuators (e.g., nodding while speaking).
  - (c) The primitive library shall be extensible without modifying the core AI loop.
  - (d) The LLM shall be informed of available primitives via the system prompt so it can reference them by name.

**R14 - Speech Output**: When the LLM produces speech text, the action generation layer shall:
  - (a) Feed the text to the TTS pipeline (R19) to produce audio.
  - (b) Stream the resulting audio to the robot's speaker via roz_host.
  - (c) Generate synchronized jaw movements that track the audio envelope, using the protocol's sync tag mechanism to coordinate jaw actuator commands with audio stream data.
  - (d) Support interruption -- if new high-priority input arrives while the robot is speaking (e.g., the human interrupts), the system shall be able to halt speech output and jaw motion and begin processing the new input.

**R15 - Action Concurrency**: The action generation layer shall support executing multiple actions concurrently.
  - (a) Speech and motion shall execute in parallel (the robot can speak while moving).
  - (b) Actions targeting different actuators shall execute independently.
  - (c) Conflicting actions targeting the same actuator shall be resolved by the most recent directive (override semantics).

### 2.5 Text-to-Speech

**R16 - On-Device TTS**: The system shall include a text-to-speech pipeline that runs on the SBC.
  - (a) The TTS model shall produce natural-sounding speech audio suitable for a home assistant robot.
  - (b) The TTS model shall run on the Jetson GPU, sharing resources with the core LLM. The system shall manage GPU memory and scheduling to avoid contention.
  - (c) The TTS pipeline should support streaming synthesis -- beginning audio output before the full text is available -- to reduce perceived latency when paired with streaming LLM inference.

**R17 - TTS Audio Format**: The TTS pipeline shall produce audio in a format compatible with roz_host's audio streaming interface.
  - (a) Output format shall match one of the sample formats defined in the wire protocol (e.g., PCM signed 16-bit).
  - (b) Sample rate shall be configurable and compatible with the controller's audio output hardware.

**R18 - Voice Character**: The TTS pipeline should support configuring the robot's voice characteristics (pitch, speed, timbre) to match the robot's personality. The specific voice parameters are implementation-defined.

### 2.6 Behavior Management

**R19 - Personality**: The system shall define the robot's personality, communication style, and behavioral tendencies via a configurable system prompt and behavioral parameters.
  - (a) The personality definition shall be loaded from a configuration file, not hardcoded.
  - (b) The personality shall inform the LLM's tone, vocabulary, humor, helpfulness, and interaction style.
  - (c) The personality definition shall include the robot's name, role, and any knowledge about its environment or household.

**R20 - Conversation State**: The system shall track the state of ongoing conversations and interactions.
  - (a) The system shall maintain a conversation history suitable for inclusion in the model context (R11).
  - (b) The system shall detect conversation boundaries (e.g., prolonged silence, topic change, person leaving) and manage history accordingly.
  - (c) The system shall track who is present (if distinguishable via audio or vision) and maintain per-person conversational context where possible.

**R21 - Robot State Awareness**: The behavior manager shall maintain a model of the robot's own state and make it available to the LLM.
  - (a) Physical state: current actuator positions, active motions, faults.
  - (b) AI state: current conversation, attention focus, ongoing tasks.
  - (c) System state: controller connectivity, perception pipeline health, resource utilization.
  - (d) The LLM shall be able to reference the robot's own state when reasoning (e.g., "I can't move my neck right now, it's faulted").

**R22 - Idle Behavior**: When the robot is not engaged in conversation or a task, the behavior manager shall generate ambient idle behaviors.
  - (a) Idle behaviors may include: looking around, small random movements, reacting to sounds, tracking visual motion.
  - (b) Idle behaviors shall be interruptible -- any incoming stimulus that warrants attention shall preempt idle behavior immediately.
  - (c) The idle behavior policy shall be configurable (frequency, intensity, types of ambient motion).

### 2.7 Attention and Turn-Taking

**R23 - Attention Model**: The system shall maintain a model of what the robot is currently attending to.
  - (a) Attention shall influence gaze direction (eye actuators) and head orientation (neck actuator).
  - (b) When a person is speaking, the robot should orient toward the speaker.
  - (c) When multiple stimuli compete for attention (e.g., two people speaking, a sound from another room), the behavior manager shall resolve which stimulus gets focus.
  - (d) Attention shifts shall be reflected in physical motion (the robot turns to look at what it's attending to).

**R24 - Turn-Taking**: The system shall manage conversational turn-taking to produce natural interaction.
  - (a) The system shall detect when a human has finished speaking (via pause detection, prosody, or model inference) before generating a response.
  - (b) The system shall avoid interrupting a human mid-sentence unless the human has paused for a configurable duration.
  - (c) When the robot is speaking and is interrupted by a human, the system shall detect the interruption and yield the turn (stop speaking, begin listening).

### 2.8 Safety

**R25 - Output Safety**: The system shall filter the LLM's output to ensure the robot behaves appropriately.
  - (a) Speech output shall be screened for content that is inappropriate for a home environment (configurable content policy).
  - (b) Motion commands shall be validated against actuator limits before execution. The safety layer shall not rely solely on the controller's limit enforcement -- it shall reject obviously invalid commands before they reach roz_host.
  - (c) The system shall enforce rate limits on actuator commands to prevent the LLM from generating rapid, erratic motion that could damage hardware or appear alarming.

**R26 - Operational Safety**: The system shall maintain safe operation under failure conditions.
  - (a) If the core LLM fails to produce output within a timeout, the system shall fall back to a safe idle state rather than freezing.
  - (b) If GPU resources are exhausted, the system shall degrade gracefully (e.g., reduce perception frequency, shorten context) rather than crashing.
  - (c) The system shall respect emergency stop state reported by controllers -- no motion commands shall be issued while any controller is in emergency stop.
  - (d) On startup, the system shall verify controller connectivity and perception pipeline health before entering the active AI loop.

### 2.9 Resource Management

**R27 - GPU Scheduling**: The system shall manage shared GPU resources between the core LLM, TTS pipeline, and any vision preprocessing.
  - (a) The core LLM has priority for GPU memory and compute.
  - (b) TTS inference shall be scheduled to avoid starving the main reasoning loop.
  - (c) The system shall monitor GPU memory usage and inference latency, adjusting workload (e.g., reducing perception frequency, shortening context) if performance degrades.

**R28 - Thermal Management**: The system should monitor SBC thermal state and reduce workload if the device is thermally throttling, to maintain consistent performance rather than oscillating between full speed and thermal throttle.

### 2.10 Configuration and Extensibility

**R29 - Configuration**: The following parameters shall be configurable without code changes:
  - (a) Model selection (model path, quantization level).
  - (b) Perception parameters (audio chunk size, video frame rate, video resolution).
  - (c) Personality definition (system prompt, voice parameters).
  - (d) Idle behavior policy.
  - (e) Safety policy (content filter settings, rate limits).
  - (f) Inference parameters (temperature, max tokens, context window budget).

**R30 - Model Pluggability**: The core LLM shall be an interchangeable component. Switching to a different model (different Gemma4 size, or a different model family entirely) shall require only configuration changes and potentially a new model-specific prompt template, not changes to the perception pipeline, action generation, or behavior manager.

**R31 - Primitive Extensibility**: New motion primitives, expressions, and behaviors shall be addable via configuration or a plugin mechanism without modifying the core AI loop.

### 2.11 Long-Term Memory

**R32 - Interaction Buffer**: The system shall maintain a persistent buffer of raw interaction records on the SBC's local storage.
  - (a) Each interaction record shall capture: timestamp, audio transcript (generated post-hoc from the LLM's understanding, not a separate ASR), visual context summary, robot actions taken, conversation content, and any relevant telemetry snapshots.
  - (b) The buffer shall be append-only during active interaction. Records shall not be modified or deleted during the AI loop's active reasoning cycles.
  - (c) The buffer shall be stored in a durable format that survives reboots and power loss.
  - (d) The buffer shall be bounded by a configurable maximum size (disk space). When the limit is approached, the oldest raw records that have already been processed into long-term memory (R33) shall be eligible for eviction.

**R33 - Memory Extraction**: During idle periods (when the robot is not actively engaged in conversation or task execution), the system shall process the interaction buffer to extract structured long-term memories.
  - (a) The extraction process shall use the core LLM to analyze buffered interactions and produce metadata records including but not limited to: people encountered (names, descriptions, relationships), topics discussed, preferences learned, requests made, emotional tone, notable events, and factual information shared by users.
  - (b) Each memory record shall include: a natural language summary, a set of semantic tags or categories, the source interaction timestamp(s), and a confidence level.
  - (c) Extraction shall be incremental -- the system processes new buffer entries since the last extraction run, not the entire buffer each time.
  - (d) The extraction process shall run at lower priority than the active AI loop. If the robot transitions from idle to active (e.g., a person starts speaking), extraction shall pause immediately and yield compute resources to the perception-reasoning-action loop.

**R34 - Vector Index**: Extracted memory records shall be stored in a vector index for semantic retrieval.
  - (a) Each memory record shall be embedded into a vector representation using an embedding model suitable for on-device execution.
  - (b) The vector index shall support approximate nearest-neighbor search to retrieve memories semantically relevant to a given query.
  - (c) The index shall be persisted to disk and survive reboots.
  - (d) The index shall support incremental insertion (new memories added without rebuilding the entire index).
  - (e) The index shall be bounded by a configurable maximum number of entries. When the limit is reached, the system shall employ a retention policy (R37).

**R35 - Memory Retrieval**: The perception pipeline shall query the vector index as part of context construction for each reasoning cycle.
  - (a) Before each LLM inference call, the system shall formulate a retrieval query from the current perceptual context (what is being discussed, who is present, what the robot is doing).
  - (b) The top-k most relevant memories shall be retrieved and included in the model's context alongside the system prompt, conversation history, and perceptual input.
  - (c) The number of retrieved memories (k) and the maximum token budget allocated to memory context shall be configurable.
  - (d) Retrieved memories shall be presented to the LLM in a structured format that distinguishes them from the current conversation, so the model can reference recalled information naturally (e.g., "I remember you mentioned...").
  - (e) Retrieval latency shall be low enough to not materially increase the AI loop cycle time. The vector search shall complete within a configurable latency budget (target: <10ms).

**R36 - Memory Types**: The memory system shall support distinct categories of memories with different retention and retrieval characteristics:
  - (a) **Episodic**: Specific interactions and events (e.g., "Alice asked about the weather on Tuesday", "Bob showed me a photo of his dog").
  - (b) **Semantic**: Learned facts and preferences (e.g., "Alice prefers tea over coffee", "Bob's dog is named Max").
  - (c) **Relational**: Information about people and their relationships (e.g., "Alice and Bob are siblings", "Carol is Alice's coworker").
  - (d) The LLM-driven extraction process (R33) shall categorize each memory record by type. Different types may have different retention policies (R37).

**R37 - Memory Retention Policy**: The system shall implement a configurable retention policy for long-term memories.
  - (a) Episodic memories shall decay over time -- older episodes are candidates for eviction or summarization into more compact semantic memories.
  - (b) Semantic memories (learned facts, preferences) shall be retained indefinitely unless contradicted by newer information, in which case they shall be updated or replaced.
  - (c) When a semantic memory is updated, the system should retain the history of changes (e.g., "Alice used to prefer coffee but now prefers tea") to support natural conversation about changes.
  - (d) Relational memories shall be retained as long as the associated people remain relevant (i.e., the robot has interacted with them within a configurable recency window).
  - (e) The retention policy shall be applied during the idle extraction cycle (R33), not during active reasoning.

**R38 - Memory Consistency**: The memory system shall handle contradictions and updates.
  - (a) When new information contradicts an existing memory, the extraction process shall update the existing record rather than creating a duplicate.
  - (b) The system shall track the timestamp of the most recent evidence supporting each memory, enabling recency-weighted retrieval.
  - (c) The system should assign and update confidence levels based on the number of corroborating interactions and recency.

**R39 - Memory Privacy**: The memory system shall support privacy controls.
  - (a) A user shall be able to request that the robot forget specific information or all information about them. The system shall delete matching records from both the interaction buffer and the vector index.
  - (b) The system shall support a configurable retention period after which all memories older than the threshold are automatically purged.
  - (c) The interaction buffer and vector index shall be stored in a configurable location on the SBC's filesystem, allowing the operator to back up, inspect, or delete the memory store.

### 2.12 Observability

**R40 - Logging**: The system shall log AI loop activity at configurable verbosity:
  - (a) Perception events: audio activity detected, frame fed to model, significant telemetry changes.
  - (b) Reasoning: model input summary, inference latency, token usage, model output.
  - (c) Actions: commands issued, speech initiated/completed, motion primitives executed.
  - (d) Errors: inference failures, pipeline errors, safety filter activations.

**R41 - Telemetry Export**: The system shall export AI system metrics for monitoring:
  - (a) Inference latency (per cycle).
  - (b) Token throughput (tokens/second).
  - (c) GPU utilization and memory usage.
  - (d) Perception pipeline latency (audio-to-model, frame-to-model).
  - (e) End-to-end response latency (stimulus to first action).

**R42 - Debug Interface**: The system should provide a debug interface (accessible from roz_ui or a separate tool) that allows:
  - (a) Viewing the current model context (what the LLM is seeing).
  - (b) Viewing the raw model output before action parsing.
  - (c) Injecting text input to test the AI loop without audio/vision.
  - (d) Overriding the personality or system prompt at runtime for testing.

---

## 3. Design Notes

### 3.1 Why Direct Audio Ingestion

Conventional robot architectures use a separate ASR (automatic speech recognition) stage to convert audio to text before the LLM processes it. This adds latency (ASR processing time), loses information (prosody, tone, emotion, non-speech sounds), and adds a failure mode (ASR errors). Gemma4's native audio understanding allows the LLM to hear directly -- interpreting tone, detecting emotion, hearing environmental sounds, and understanding speech in a single step. This reduces the pipeline to one model call instead of two.

### 3.2 Continuous Visual Awareness

Rather than triggering vision on demand ("look at X"), the robot continuously feeds downsampled frames to the model. This gives the LLM ambient situational awareness -- it can notice when someone enters the room, react to visual changes, and reference what it sees in conversation without being explicitly asked to look. The cost is additional inference compute per cycle, which is managed by adaptive frame rate (R6b) and scene change detection (R6c).

### 3.3 Compound AI System vs. Single Model

Although Gemma4 handles perception (audio, vision) and reasoning in a single model, the overall system is "compound" because it includes multiple components beyond the LLM: a TTS model, a VAD module, motion primitive execution, behavior management, and safety filtering. The LLM is the cognitive core, but it operates within a larger system that preprocesses its inputs and interprets its outputs.

### 3.4 Jaw Synchronization

Synchronized jaw movement during speech is a key element of embodied presence. The approach: the TTS pipeline produces audio chunks, the action generator analyzes each chunk's amplitude envelope to determine jaw position, and both the audio stream data and jaw actuator commands are sent to the controller with matching sync tags (per wire protocol Section 4.4). This happens at the audio chunk level (~20-50ms granularity), not per-sample. The LLM is not involved in per-chunk jaw positioning -- it decides *what* to say, not how the jaw moves while saying it.

### 3.5 Inference Latency Budget

The target end-to-end latency from stimulus to first robot action constrains the architecture. On the Jetson Orin Nano with a quantized model:

| Stage | Target |
|---|---|
| Audio chunk capture | ~100 ms |
| Perception preprocessing | ~10 ms |
| LLM inference (first token) | ~200-500 ms |
| Action parsing | ~1 ms |
| TTS first audio chunk | ~100-200 ms |
| Protocol + transport | ~5 ms |
| **Total to first speech** | **~400-800 ms** |

These are rough targets. Streaming inference (R10c) and streaming TTS (R16c) are critical to hitting the lower end -- the robot can begin speaking before the LLM has finished generating the full response.

### 3.6 Multi-Model GPU Sharing

The Jetson Orin Nano has 8GB of unified memory shared between CPU and GPU. The core LLM and TTS model must coexist. Strategies:

- Load the LLM permanently; load/unload TTS on demand (adds latency to first speech).
- Keep both loaded with aggressive quantization (reduces quality).
- Use a TTS model small enough to coexist (e.g., ~500MB alongside a 4-bit quantized LLM).

The right balance is an implementation decision that depends on the specific model sizes. The requirements (R27) mandate that the system manages this, but do not prescribe the strategy.

### 3.7 Long-Term Memory Architecture

The memory system uses a two-stage pipeline: capture then extract.

**Stage 1 (Active):** During active interaction, the system appends raw interaction records to a local buffer. This is cheap — it's mostly a structured log of what happened. No LLM inference is needed beyond what the main reasoning loop already does. The buffer is the ground truth from which memories are derived.

**Stage 2 (Idle):** When the robot is idle, the extraction process reads unprocessed buffer entries, feeds them to the LLM with a memory-extraction prompt, and writes the resulting structured memories into the vector index. This is where the expensive work happens — the LLM analyzes interactions to extract facts, preferences, relationships, and notable events. Running this during idle time ensures it never competes with the active perception-reasoning-action loop for GPU resources.

**Retrieval at inference time:** Before each reasoning cycle, the perception pipeline formulates a query from the current context (who's present, what's being discussed) and retrieves relevant memories via vector similarity search. This adds a small constant cost to each cycle (~10ms for the vector search) but gives the LLM access to arbitrarily old information without consuming context window space for the full history.

**Why vector search, not full-text or recency-based?** The robot needs to recall information that is *semantically* relevant, not just recent. If Alice mentioned her dog's name three weeks ago and now asks "do you remember my dog?", a recency-based buffer won't help — the dog conversation is long gone from the rolling context. But a vector search for "Alice's dog" will find the semantic memory. The embedding model for vector search is small (~100MB) and runs on CPU, leaving the GPU free for the main LLM.

**Episodic vs. semantic vs. relational:** The distinction matters for retention. Episodic memories ("Alice visited on Tuesday") become less useful over time and can be summarized or evicted. Semantic memories ("Alice prefers tea") remain useful indefinitely. Relational memories ("Alice and Bob are siblings") provide structural context that improves the robot's social awareness. The extraction LLM categorizes each memory, and the retention policy treats each category differently.

### 3.8 Relationship to ROS2

roz_ai does not depend on ROS2 directly. It communicates with the robot through roz_host's Python API. A future ROS2 integration could either:
- Run roz_ai as a ROS2 node that publishes/subscribes to topics (wrapping roz_host calls in ROS2 message types).
- Keep roz_ai independent and run a separate bridge node.

The AI system's architecture does not need to change for either approach.
