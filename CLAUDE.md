# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**ROZ (Robot Orchestration Zero)** is a robotic home assistant platform. The system comprises embedded controllers for actuator/sensor management, a shared protocol library, a host-side communications library, a compound AI system (multimodal LLM), and an operator interface. All projects live in this monorepo and communicate via a custom binary protocol.

## Repository Structure

```
roz1/
├── CLAUDE.md               # this file -- repo-wide guidance
├── roz_docs/               # all documentation (requirements, design, protocol spec)
│   ├── index.md            # documentation entry point
│   ├── system/             # system-level architecture
│   ├── protocol/           # wire protocol specification
│   ├── controller/         # embedded firmware requirements & design
│   ├── proto_lib/          # shared protocol library requirements & design
│   ├── host_lib/           # host library requirements & design
│   ├── ai/                 # AI system requirements & design
│   └── ui/                 # operator UI requirements & design
├── roz_firmware/           # embedded controller firmware (C, bare metal, STM32)
├── roz_proto/              # shared protocol library (C, portable, no OS deps)
├── roz_host/               # base station host library (C, Linux, Python bindings)
├── roz_ai/                 # compound AI system (Python, Gemma4, Jetson) [future]
└── roz_ui/                 # operator terminal UI (Python, Textual) [future]
```

### Project Dependency Graph

```
roz_ai (Python, SBC)       roz_ui (Python)
    │                          │
    │ Python API               │ Python API
    ├──────────────┬───────────┘
    │              │
    ▼              ▼
roz_host (C, Linux)
    │ C API
    ▼
roz_proto (C, portable)
    │               │
    │ link/include  │ link/include
    ▼               ▼
roz_host          roz_firmware
(internals)       (embedded)
```

Dependencies flow strictly downward. roz_proto has zero OS or hardware dependencies.

### Project Summary

| Project | Language | Target | Builds To |
|---|---|---|---|
| roz_firmware | C (bare metal) | Cortex-M0+ (STM32L031K6) | .elf |
| roz_proto | C99 (no extensions) | Portable (bare metal + Linux) | .a (static lib) |
| roz_host | C99 (POSIX) | Linux (x86_64, aarch64) | .so (shared), .a (static) |
| roz_ai | Python | Jetson Orin Nano (aarch64) | Python package |
| roz_ui | Python | Linux | Python package |

## Documentation Conventions

All documentation lives in `roz_docs/`, not in individual project directories. Each project has up to two documents:

### Requirements Document (`requirements.md`)

Defines **what** the system must do -- its properties, constraints, and behaviors. Requirements documents are formal and auditable.

**Format:**

- **Numbered sections** organize requirements by domain (e.g., `### 2.1 Hardware Abstraction`, `### 2.3 Communication`).
- **R-numbered requirements** state individual properties. Each requirement has a unique ID that is stable across revisions. Format: `**R<number> - <Short Name>**: <requirement text>`.
- **Sub-items** use `(a)`, `(b)`, `(c)` lettering for enumerated properties within a requirement.
- **Overview section** (Section 1) establishes scope, system context, and references to other documents.
- **Design Notes section** (final section) captures rationale, trade-offs, and context that inform the requirements but are not requirements themselves.
- Requirements use **shall** for mandatory behavior, **should** for recommended behavior, and **may** for optional behavior.
- Requirements do not specify implementation strategy -- that belongs in the design document.

Example:

```markdown
### 2.1 Framing

**R1 - COBS Encoding**: The library shall encode all outgoing frames and decode all
incoming frames using Consistent Overhead Byte Stuffing (COBS), as defined in
[wire_protocol.md](../protocol/wire_protocol.md) Section 2.1.

**R2 - Frame Integrity**: The library shall:
  - (a) Compute a CRC-16/CCITT checksum over the header and payload of every outgoing frame.
  - (b) Validate the CRC-16/CCITT checksum of every incoming frame.
  - (c) Report frames with invalid checksums to the caller via error callback.
```

### Design Document (`design.md`)

Defines **how** the system is built -- module structure, data types, interfaces, algorithms, data flow, and resource budgets. Design documents are technical and implementation-oriented.

**Format:**

- **Module descriptions** with C interface definitions (structs, function signatures).
- **Data flow diagrams** showing how data moves through the system.
- **State machines** where applicable.
- **Memory/resource budgets** for constrained targets.
- Design documents reference requirements by R-number (e.g., "implements R5") to maintain traceability.
- Design documents may include rationale for implementation choices.

### Relationship Between Requirements and Design

Requirements are stable and change infrequently. Design documents evolve as implementation progresses. A requirement states "the system shall interpolate actuator positions" (what); the design document defines the interpolation algorithm, tick rate, and data structures (how).

Every requirement should be traceable to at least one design element. Every design element should trace back to at least one requirement.

## Code Conventions

### C Code (roz_firmware, roz_proto, roz_host)

- **Standard:** C99, no compiler-specific extensions in shared code (roz_proto).
- **Naming:** `snake_case` for functions and variables, `UPPER_SNAKE_CASE` for constants and macros, `type_name_t` suffix for typedefs.
- **Prefix:** Public API functions use project prefix (`roz_proto_`, `roz_host_`). Internal/static functions do not need a prefix.
- **No dynamic allocation** in roz_proto or roz_firmware. roz_host may use malloc for connection management.
- **State passing:** Functions receive state via pointer to caller-owned context structs. No file-scoped static mutable state in roz_proto (required for re-entrancy and multi-instance use).

### Python Code (roz_ai, roz_ui)

- Standard Python 3 conventions (PEP 8).

## Build

### roz_firmware

```bash
make -C roz_firmware/Debug        # build (output: Debug/servo_controller_1.elf)
make -C roz_firmware/Debug clean  # clean
```

Toolchain: `arm-none-eabi-gcc`. See `roz_firmware/CLAUDE.md` for peripheral map, clock config, and CubeMX notes.

**Note:** CubeMX artifacts use legacy name `servo_controller_1`. The project has been renamed to `roz_firmware`.

### roz_proto, roz_host

Build system TBD (CMake planned). Not yet implemented.

## Key Documents

| Document | Path |
|---|---|
| Documentation index | `roz_docs/index.md` |
| System architecture | `roz_docs/system/architecture.md` |
| Wire protocol spec | `roz_docs/protocol/wire_protocol.md` |
| Firmware requirements | `roz_docs/controller/requirements.md` |
| Firmware module design | `roz_docs/controller/module_design.md` |
| Proto lib requirements | `roz_docs/proto_lib/requirements.md` |
| Host lib requirements | `roz_docs/host_lib/requirements.md` *(in progress)* |
| AI system requirements | `roz_docs/ai/requirements.md` |
