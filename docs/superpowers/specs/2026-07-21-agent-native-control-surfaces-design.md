# Agent-Native Control Surfaces Design

Date: 2026-07-21
Baseline: Open Ephys GUI 1.0.2, commit `c91afebcfb0678a667fb93f6312ed33c56ec640f`
Branch: `agent-native-v102`

## Purpose

This branch changes only how existing Open Ephys behavior is exposed. It does
not add acquisition, recording, analysis, visualization, or instrument
features.

The branch has two deliverables:

1. Every existing GUI operation and observable state is available through
   Windows UI Automation (UIA).
2. The same operation and state is available through the Open Ephys built-in
   HTTP API.

GUI, UIA, and API behavior must remain equivalent. Coordinate-based automation
and an external agent server are outside this design.

## Design principles

- Existing Open Ephys behavior is authoritative. UIA and API are adapters over
  the same actions and state, not alternate business logic.
- One canonical capability identifier describes the same feature everywhere.
- Existing `/api/*` routes remain compatible.
- Dynamic processors, streams, parameters, channels, electrodes, and plugin
  controls are described at runtime.
- Every change is reviewable as a small commit and can be proposed separately
  to the official `development` branch.
- Instrument safety and acquisition-time restrictions are identical across
  GUI, UIA, and API callers.

## Canonical capability model

Each exposed feature has a `ControlCapability` descriptor with:

| Field | Meaning |
|---|---|
| `id` | Stable namespaced identifier, beginning with `oe.` |
| `name` | Concise human-readable name used by UIA and API discovery |
| `description` | Exact functional or scientific meaning |
| `kind` | `action`, `toggle`, `value`, `range`, `selection`, `collection`, or `status` |
| `operations` | Supported `read`, `invoke`, `set`, `toggle`, `expand`, or `select` operations |
| `state` | Current value and enabled, visible, selected, checked, or expanded state |
| `constraints` | Type, units, minimum, maximum, step, allowed values, and read-only state |
| `availability` | Why an operation is enabled or blocked in the current OE state |
| `api` | HTTP methods, route templates, request fields, and response fields |
| `uia` | AutomationId, role, supported patterns, and help text |

The canonical ID is also the Windows AutomationId. API discovery returns that
same ID. Existing API paths do not need to equal the ID, but their relationship
is explicit in the descriptor.

Examples:

| Capability ID | UIA | API |
|---|---|---|
| `oe.control.acquisition` | Toggle/Invoke button | `GET/PUT /api/status` |
| `oe.control.recording` | Toggle/Invoke button | `GET/PUT /api/status` |
| `oe.status.cpu_usage` | Read-only RangeValue | `GET /api/cpu` |
| `oe.status.disk_usage` | Read-only RangeValue | `GET /api/disk` |
| `oe.status.elapsed_time` | Read-only Value | `GET /api/time` |
| `oe.parameter.<key>` | Type-specific UIA pattern | Existing parameter GET/PUT routes |

## Identity rules

Static IDs use semantic names:

```text
oe.window.main
oe.menu.file.open
oe.control.acquisition
oe.status.cpu_usage
```

Dynamic IDs use persistent Open Ephys identities where available:

```text
oe.processor.<node_id>
oe.processor.<node_id>.stream.<stream_key>
oe.processor.<node_id>.parameter.<parameter_key>
oe.processor_catalog.<processor_slug>
oe.visualizer.<node_id>.<visualizer_slug>
```

Dynamic segments are lower-case ASCII letters, digits, and underscores.
Characters outside that set collapse to one underscore. Generated IDs must be
unique within the running application. A collision is a test failure and is
reported by capability discovery.

## Architecture

### Capability registry

The registry owns descriptors and resolves runtime instances. Static core
controls register during application construction. Processor and plugin
instances contribute capabilities using their existing metadata, parameters,
and editor components.

The registry does not own GUI components, processors, or parameters. Runtime
entries use safe references and disappear when their source object is removed.

### Shared operations

Capabilities call existing application operations:

- `CoreServices` for acquisition, recording, configuration, messages, and
  record-node behavior.
- `ProcessorGraphActions` and existing undoable actions for graph changes.
- `Parameter` validation and `setNextValue` for parameter changes.
- Existing button, selector, editor, popup, and command callbacks where no
  lower-level action currently exists.

When a GUI callback contains business logic, that logic is extracted into one
callable operation. The GUI callback, UIA action, and API route then call the
same operation. The operation is not duplicated in an HTTP handler.

### UIA adapter

The UIA adapter applies the descriptor ID, name, description, help, role, and
state to the corresponding JUCE component. It supplies the appropriate JUCE
accessibility interface:

- Invoke for actions.
- Toggle for Boolean state.
- Value for text and scalar state.
- RangeValue for bounded numbers and meters.
- Selection and SelectionItem for lists, tables, tabs, and channel groups.
- ExpandCollapse for drawers, menus, popup controls, and trees.

Changes notify the matching JUCE accessibility event. Hidden controls remain
addressable through their containing capability and appear in the UIA tree
when the existing GUI makes them visible.

### HTTP API adapter

The existing `OpenEphysHttpServer` remains the transport on port 37497.
Existing routes remain functional. New routes fill GUI coverage gaps rather
than replacing compatible routes.

`GET /api/capabilities` returns the runtime manifest. It supports filtering by
ID prefix, processor ID, kind, and operation. Each entry includes current state,
constraints, UIA metadata, and API operation metadata.

API responses use a consistent envelope for new routes:

```json
{
  "ok": true,
  "capability": "oe.status.disk_usage",
  "state": {
    "value": 0.42,
    "minimum": 0.0,
    "maximum": 1.0,
    "read_only": true
  }
}
```

Errors use:

```json
{
  "ok": false,
  "capability": "oe.control.recording",
  "error": {
    "code": "operation_not_available",
    "message": "Recording requires at least one configured Record Node."
  }
}
```

Existing routes retain their current response shapes for compatibility. Their
capability descriptors document those shapes.

## Threading and safety

The HTTP server continues to run off the message thread. Operations that touch
JUCE components or application state are dispatched to the message thread and
return only after completion or a bounded timeout. Read-only immutable metadata
may be returned directly.

The shared operation performs validation once. The result is returned to the
API caller, reflected in UIA state, and shown through the existing GUI status
mechanism when appropriate.

The following invariants apply:

- No API or UIA path bypasses acquisition-time locks.
- No write occurs when the corresponding GUI control is disabled.
- Parameter type, range, allowed-value, stream, and channel validation is
  identical for GUI and API callers.
- Destructive graph and configuration actions remain undoable when the GUI
  operation is undoable.
- File operations use the same existence, overwrite, and active-acquisition
  checks as the GUI.
- API error responses never report success before the message-thread operation
  has completed.

## Coverage inventory

The completion inventory is organized by capability families:

1. Application window, menus, commands, popup menus, and file dialogs.
2. Acquisition, recording, destination, timing, health, and audio controls.
3. Processor catalog, search, categories, and processor items.
4. Signal chain, processor editors, drawers, streams, routing, and placeholders.
5. All shared parameter editor families and their popup contents.
6. Console, messages, info, graph, tabs, visualizers, and secondary windows.
7. Startup configuration, plugin installer, updater, and progress/error dialogs.
8. Core processor-specific custom controls.
9. Bundled plugin custom controls, including dynamic channels and electrodes.

A family is complete only when every runtime instance has equivalent GUI, UIA,
and API read/action coverage.

## Testing strategy

### Unit tests

- Capability ID validation and dynamic-segment sanitization.
- Descriptor serialization and operation metadata.
- Action, toggle, value, range, selection, and collection state.
- Validation and error results for disabled or invalid operations.
- Shared-operation tests proving GUI and API adapters call the same behavior.
- Component tests for UIA role, name, description, patterns, and state.

### Contract tests

- Every UIA `oe.*` ID appears exactly once in the runtime capability manifest.
- Every manifest entry exposes at least one working API read or action route.
- Every mutable GUI feature has both a UIA action and an API operation.
- UIA and API report the same value, range, units, enabled state, and selection.
- Dynamic add/remove operations update both surfaces without stale entries.

### Runtime tests on Windows

- Enumerate the complete UIA tree and record roles, patterns, values, and state.
- Query `/api/capabilities` in the same application state.
- Compare both surfaces by canonical ID.
- Invoke representative safe operations through UIA and API and verify the
  same state transition.
- Exercise expanded/collapsed panels, menus, dialogs, dynamic processors,
  streams, parameters, and bundled plugins.

### Regression gates

- Official v1.0.2 unit and component tests remain green.
- Release source build completes.
- The official baseline workflows remain visually and functionally unchanged.
- No duplicate or empty canonical IDs.
- No manifest entry lacks API coverage at final completion.
- No discoverable actionable UIA control lacks the appropriate UIA pattern.

## Delivery and upstream contribution

Development remains a sequence of small changes:

1. Capability model and discovery endpoint.
2. Core global controls and status parity.
3. Dynamic parameter parity.
4. Menus, commands, dialogs, and utility panels.
5. Processor catalog and signal-chain parity.
6. Core processor custom-control parity.
7. Bundled plugin parity.
8. Full Windows conformance inventory and completion audit.

Portable changes are independently rebased onto the latest official
`development` branch. Each upstream contribution starts with a focused issue
and follows the official contribution templates. The v1.0.2 agent branch is
the compatibility baseline, not the branch submitted wholesale upstream.
