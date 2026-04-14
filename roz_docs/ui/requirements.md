# ROZ Operator Interface (roz_ui) - Requirements Document

## 1. Overview

This document defines the requirements for the operator terminal UI. roz_ui is a Textual-based terminal application that provides real-time visibility into one or more ROZ robots and their subsystems. It displays telemetry pushed from connected robots, shows system health at a glance, and will support operator commands for manual control.

### 1.1 Scope

This project covers the **terminal user interface only**. roz_ui consumes robot state via roz_server's network API. It does not implement protocol logic, actuator control algorithms, or AI reasoning -- those are handled by roz_proto, roz_firmware, roz_control, and roz_ai respectively.

### 1.2 System Context

```
                      ┌──────────────────────────────────────┐
                      │           Operator Terminal           │
                      │                                      │
                      │  ┌────────────────────────────────┐  │
                      │  │            roz_ui               │  │
                      │  │  (Textual TUI, this project)    │  │
                      │  └───────────────┬────────────────┘  │
                      └──────────────────┼──────────────────┘
                                         │ network
                          ┌──────────────┼──────────────┐
                          │    Robot     │              │
                          │              ▼              │
                          │  ┌────────────────────┐     │
                          │  │  SBC               │     │
                          │  │  roz_server        │     │
                          │  │   └─ roz_ai        │     │
                          │  │   └─ roz_control   │     │
                          │  └────────┬───────────┘     │
                          │           │ UART            │
                          │  ┌────────┴───────────┐     │
                          │  │  Controller(s)     │     │
                          │  │  (roz_firmware)    │     │
                          │  └────────────────────┘     │
                          └─────────────────────────────┘
```

### 1.3 Reference Documents

- [System Architecture](../system/architecture.md) -- system context and project relationships.
- [Wire Protocol](../protocol/wire_protocol.md) -- the underlying protocol (for understanding telemetry and state semantics).
- [Controller Requirements](../controller/requirements.md) -- embedded controller capabilities and state model.
- [AI System Requirements](../ai/requirements.md) -- AI system state and observability (UI-R40-R42).
- [Server Requirements](../server/requirements.md) -- the server daemon through which roz_ui receives robot state and issues commands.

---

## 2. Requirements

### 2.1 Global Layout

**UI-R1 - Banner**: Every view shall display a persistent top banner containing:
  - (a) An ASCII art "ROZ CONTROL" title.
  - (b) A robot ASCII art icon (see UI-R2).
  - (c) Global connection summary (number of robots connected / total configured).
  - (d) Current time and session uptime.

**UI-R2 - Robot Mood Icon**: The banner shall include a small ASCII robot icon whose expression reflects system state:
  - (a) On the Robot List View, the icon shall reflect the aggregate state of all connected robots (e.g., happy when all healthy, concerned when warnings exist, distressed when errors are present).
  - (b) On robot-specific views (Robot Detail, Controller Detail, Actuator, Sensor), the icon shall reflect the state of the currently selected robot.
  - (c) The icon shall visually distinguish at minimum: healthy, warning, error/fault, and disconnected states.

**UI-R3 - Contextual Key Hints**: The banner shall display a row of available keyboard commands relevant to the current view. The key hints shall update when the user navigates to a different view.

**UI-R4 - Color Coding**: The UI shall use consistent color coding throughout all views to convey status at a glance:
  - (a) Green for healthy / connected / nominal state.
  - (b) Yellow/amber for warning / degraded state.
  - (c) Red for error / fault / critical state.
  - (d) Grey or dim for disconnected / unavailable / inactive state.
  - (e) Color assignments shall be consistent across all views and table columns.

### 2.2 Navigation

**UI-R5 - Drill-Down Navigation**: The UI shall follow a hierarchical drill-down navigation model inspired by k9s:
  - (a) Pressing `Enter` on a selected row navigates into the detail view for that item.
  - (b) Pressing `Escape` navigates back to the parent view.
  - (c) The navigation hierarchy is: Robot List -> Robot Detail -> Controller Detail.

**UI-R6 - Shortcut Views**: From any robot-scoped view (Robot Detail, Controller Detail), the following shortcut keys shall be available:
  - (a) `a` -- navigate to the Actuator View for the current robot.
  - (b) `s` -- navigate to the Sensor View for the current robot.
  - (c) `l` -- navigate to the Log View for the current robot.

**UI-R7 - Command Bar**: The UI shall provide a command bar at the bottom of the screen:
  - (a) Pressing `:` shall open the command bar for command input (e.g., `:help`, `:quit`).
  - (b) Pressing `/` shall open the command bar in filter mode, allowing the user to filter the current table's rows by a text pattern.
  - (c) The command bar shall close on `Escape` or after command execution.

### 2.3 Help System

**UI-R8 - Contextual Help Modal**: Pressing `?` or entering `:h` or `:help` in the command bar shall open a help modal:
  - (a) The modal shall fill most of the screen and be easy to read.
  - (b) Help content shall be contextual -- it shall describe the commands, key bindings, columns, and behaviors specific to the current view.
  - (c) The modal shall be dismissible by pressing `Escape` or `?`.

### 2.4 Robot List View (Entry Screen)

**UI-R9 - Robot List Table**: The entry screen shall display a table with one row per configured robot. Each row shall display at minimum:
  - (a) Robot name / identifier.
  - (b) Connection status (connected, disconnected, connecting).
  - (c) Error state (healthy, warning, fault -- reflecting the worst-case status across all controllers).
  - (d) Uptime (time since the robot's connection was established).
  - (e) Battery level (if reported by the robot).
  - (f) Number of controllers (connected / total).
  - (g) A brief status summary or most recent event.

**UI-R10 - Robot Selection**: The user shall be able to navigate the robot list using arrow keys or `j`/`k` (vim-style). Pressing `Enter` on a robot row shall navigate to the Robot Detail View for that robot.

### 2.5 Robot Detail View

**UI-R11 - SBC Status Sub-Banner**: The Robot Detail View shall display a sub-banner below the global banner showing SBC-level state for the selected robot, including:
  - (a) SBC connection status.
  - (b) AI system status (running, idle, error -- if roz_ai is active).
  - (c) Resource utilization summary (CPU, GPU, memory, temperature -- as reported by the SBC).
  - (d) Robot-level error or warning summary.

**UI-R12 - Controller Table**: Below the SBC sub-banner, the Robot Detail View shall display a table with one row per controller. Each row shall display at minimum:
  - (a) Controller name / identifier.
  - (b) Connection status.
  - (c) Error state (healthy, warning, fault).
  - (d) Number of actuators and sensors.
  - (e) Firmware version.
  - (f) Uptime.

**UI-R13 - Controller Selection**: The user shall be able to navigate the controller table using arrow keys or `j`/`k`. Pressing `Enter` on a controller row shall navigate to the Controller Detail View for that controller.

### 2.6 Controller Detail View

**UI-R14 - Controller Detail**: The Controller Detail View shall display the actuators and sensors belonging to the selected controller:
  - (a) An actuator table listing each actuator with: name/ID, type, current position, target position, status (active, idle, fault), and hold mode.
  - (b) A sensor table listing each sensor with: name/ID, type, current value or status, sample rate, and health.
  - (c) A controller status header showing: controller name, firmware version, connection state, error flags, and uptime.

### 2.7 Actuator View

**UI-R15 - All-Actuator Table**: The Actuator View (accessed via `a` from any robot-scoped view) shall display a table of all actuators across all controllers for the selected robot. Each row shall display at minimum:
  - (a) Controller name (to identify which controller owns the actuator).
  - (b) Actuator name / identifier.
  - (c) Type.
  - (d) Current position.
  - (e) Target position.
  - (f) Status (active, idle, fault).

### 2.8 Sensor View

**UI-R16 - All-Sensor Table**: The Sensor View (accessed via `s` from any robot-scoped view) shall display a table of all sensors across all controllers for the selected robot. Each row shall display at minimum:
  - (a) Controller name (to identify which controller owns the sensor).
  - (b) Sensor name / identifier.
  - (c) Type.
  - (d) Current value or reading.
  - (e) Status (active, inactive, fault).

### 2.9 Log View

**UI-R17 - Log View**: The Log View (accessed via `l` from any robot-scoped view) shall display a scrollable, chronological list of events and log messages for the selected robot:
  - (a) Log entries shall include: timestamp, source (controller name, SBC, AI system), severity (info, warning, error), and message text.
  - (b) The view shall auto-scroll to show the most recent entries, with the ability to scroll back through history.
  - (c) The user shall be able to filter log entries using the `/` filter command.

### 2.10 Connection and Configuration

**UI-R18 - Configuration-Driven Connections**: Robot connections shall be driven by a configuration file:
  - (a) The configuration file shall specify all robots the UI should connect to, including connection parameters sufficient for roz_server to establish communication.
  - (b) On startup, the UI shall read the configuration file and begin connecting to all configured robots.
  - (c) The UI shall not require restart to detect robots -- but the initial robot inventory is defined by the configuration file.

**UI-R19 - State Advertisement**: Each connected robot (via roz_server) shall push its full state to the UI:
  - (a) roz_server shall advertise all robot state, AI system state, controller inventories, actuator/sensor registries, and telemetry to the UI without the UI needing to poll or request it.
  - (b) The UI shall update its views in real time as state updates are received.
  - (c) Commands from the UI to the robot shall be sent asynchronously and shall not block the UI while awaiting acknowledgment.

### 2.11 Responsiveness

**UI-R20 - Non-Blocking UI**: The UI shall remain responsive at all times:
  - (a) A slow, unresponsive, or disconnected robot connection shall not block or freeze the UI.
  - (b) All network I/O and protocol communication shall run asynchronously, separate from the UI rendering loop.
  - (c) If a robot becomes unreachable, its status shall update to reflect the disconnection, and the user shall be able to continue interacting with other robots and views without interruption.

### 2.12 Sorting and Filtering

**UI-R21 - Table Sorting**: All table views should support column-based sorting where meaningful (e.g., by name, status, controller).

**UI-R22 - Table Filtering**: All table views shall support text-based row filtering via the `/` command bar (UI-R7b). The filter shall match against visible column content and hide non-matching rows.

---

## 3. Design Notes

### 3.1 k9s Navigation Paradigm

The UI adopts the k9s (Kubernetes CLI dashboard) interaction model: a hierarchy of table views where `Enter` drills into detail and `Escape` returns to the parent. This paradigm is well-suited to the robot's hierarchical structure (robots -> controllers -> actuators/sensors) and is familiar to operators who use terminal tools. The `:` command bar and `/` filter bar follow the same convention.

### 3.2 Textual Framework

roz_ui is built on the [Textual](https://textual.textualize.io/) framework for Python terminal UIs. Textual provides rich widget support (tables, modals, styled text), async-first architecture (important for non-blocking network I/O), and cross-terminal compatibility. Its reactive data binding model is a natural fit for a UI that receives pushed state updates.

### 3.3 Push-Based Data Model

The UI is a passive consumer of state. Robots push telemetry, AI state, and state updates to the UI via roz_server; the UI does not poll. This simplifies the UI's responsibilities (render what you're told, don't ask for it) and aligns with the wire protocol's event-driven telemetry model. Commands flow in the opposite direction -- the UI pushes commands asynchronously to the robot via roz_server.

### 3.4 Multi-Robot Support

The UI is designed from the start to manage multiple robots. Even if the initial deployment has a single robot, the architecture (robot list as the entry screen, per-robot views, per-robot log streams) ensures that multi-robot support is not a retrofit. The configuration file defines the robot inventory, and each robot's state is independent in the UI.

### 3.5 ASCII Robot Mood Icon

The mood icon is a small but meaningful detail for operator awareness. A quick glance at the banner tells the operator whether the system is healthy without reading any tables. The icon's mood mapping (healthy/warning/error/disconnected) parallels the color coding scheme (green/yellow/red/grey), providing redundant visual signals -- useful for operators in environments where terminal colors may not render reliably.
