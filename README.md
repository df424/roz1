# ROZ (Robot Orchestration Zero)

A robotic home assistant platform with actuated features, audio I/O, a camera, and a multimodal AI system. The robot perceives its environment through continuous audio and visual awareness, reasons using an on-device LLM, and acts through coordinated motion and speech.

## Projects

| Project | Description | Language |
|---|---|---|
| [roz_firmware](roz_firmware/) | Embedded controller firmware (bare metal, STM32) | C |
| [roz_proto](roz_proto/) | Shared protocol library (portable, no OS deps) | C |
| [roz_host](roz_host/) | Base station host library (Linux, Python bindings) | C |
| [roz_ai](roz_ai/) | Compound AI system (Gemma4, Jetson Orin Nano) | Python |
| [roz_ui](roz_ui/) | Operator terminal UI (Textual) | Python |

## Documentation

All project documentation lives in [roz_docs/](roz_docs/).
