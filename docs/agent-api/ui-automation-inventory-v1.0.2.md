# Open Ephys GUI 1.0.2 Windows UI Automation inventory

Date: 2026-07-20  
Baseline: official Open Ephys GUI v1.0.2, commit `c91afebcfb0678a667fb93f6312ed33c56ec640f`  
Scope: expose existing 1.0.2 behavior to Windows UI Automation and agents. This inventory does not propose new scientific or workflow features.

## Status definitions

| Status | Meaning |
|---|---|
| Complete | A Windows UIA client can discover the control, identify it stably, read its state, and invoke the existing action. |
| Partial | Some source label, tooltip, name, command description, or component ID exists, but the control is not completely usable through UIA. |
| Missing | The existing GUI feature is not discoverable/operable through UIA. |

## Executive result

| Area | Complete | Partial | Missing |
|---|---:|---:|---:|
| Native top-level window shell | 1 | 0 | 0 |
| Open Ephys business controls | 0 | some source metadata | all runtime controls |

The installed official v1.0.2 build exposes only:

```text
Window: "Open Ephys GUI", App: open-ephys.exe
  window Open Ephys GUI; Secondary Action: Raise
```

The result was unchanged with the File menu open and after it was closed. No menu, menu item, button, editor, slider, combo box, table, tab, processor, parameter, message control, or visualizer descendant appeared in the Windows UIA tree.

The primary source-level blocker is `Source/MainWindow.cpp`, where the main window calls `setAccessible(false)`. Fixing that gate is necessary but not sufficient: controls still need stable semantic names, roles, states, actions, and Automation IDs.

Static metadata in the application and bundled-plugin sources (`Source` and `Plugins`, excluding vendored JUCE) is sparse and inconsistent:

- `setAccessible`: 1 occurrence, disabling the main window.
- `createAccessibilityHandler`, `AccessibilityHandler`, `AccessibilityActions`, and `AccessibilityValueInterface`: 0 occurrences.
- `setComponentID`: 3 assignments. They do not form a stable application-wide schema.
- `setName`: 29 occurrences.
- `setTooltip`: 60 occurrences.
- UI component constructor references: approximately 677 across `Source` and bundled `Plugins`.

Existing metadata is useful input for accessible names/descriptions, but it is not currently an agent interface.

## Complete now

| Surface | Existing UIA exposure | Assessment |
|---|---|---|
| Open Ephys native top-level window | Name `Open Ephys GUI`, control type `Window`, `Raise` secondary action | Complete only as an OS window shell; no Open Ephys feature is exposed below it. |

## Partial now

| Source area | What exists | Why still partial |
|---|---|---|
| Main menu | `setName("MainMenu")`; command names/descriptions through JUCE command info | No runtime UIA menu descendants or invokable menu items. |
| General controls | 29 names and 60 tooltips | Coverage is incomplete, names are not governed by a stable schema, and controls are absent from the runtime UIA tree. |
| Spike Detector Configure | Component ID `config_spikes` | Isolated/non-namespaced ID; not visible through UIA. |
| Record Node monitor | Dynamic component ID made from source-node ID and stream name | Display-oriented and unstable as a public Automation ID; not visible through UIA. |
| Mask-channel parameter | Component ID derived from the parameter key | Useful seed, but only one parameter family and not visible through UIA. |
| Menus and commands | Human-readable action descriptions | Descriptions do not expose control roles, checked/enabled state, invocation, or Automation IDs. |

## Missing: complete GUI surface inventory

Every item below exists in the 1.0.2 GUI but is missing as a complete Windows UIA control. Dynamic rows/channels/electrodes/streams are listed as families; every runtime instance of each family is included.

### 1. Application shell and menus

| Surface | Controls/actions requiring UIA |
|---|---|
| File menu | Open; Save; Save As; Reload on startup; Enable HTTP Server; Load a default config; Plugin Installer; Quit |
| Edit menu | Undo; Redo; Copy; Paste; Clear signal chain; Lock signal chain |
| View menu | Processor List; Signal Chain; File Info; Info Tab; Graph Viewer; Console; Message Window |
| View / clock | Default display; HH:MM:SS; Cumulative reference; Acquisition-start reference |
| View / appearance | Light, Medium, Dark themes; Windows Software/CPU renderer; Direct2D/GPU renderer; Reset window bounds |
| Help menu | Online documentation; Check for updates |
| Popup/context menus | Processor actions, tab actions, color selection, parameter choices, and all dynamically generated popup items |
| File dialogs | Open/save/path chooser controls, selected path, file list, confirmation and cancellation |

Required state includes enabled/disabled, checked/unchecked, selected value, expanded/collapsed, keyboard shortcut, and invocation result where JUCE exposes it.

### 2. Global control panel

| Group | Controls/state requiring UIA |
|---|---|
| Acquisition | Play/start acquisition; stop acquisition; acquisition state |
| Recording | Record/start; stop; record state; recording options expander |
| Recording destination | Filename/path display and editor; record-engine selector; record-options button; new directory; force new directory |
| Timing | Clock value; clock mode/reference already available in menus |
| Health meters | CPU meter value; disk meter value/status |
| Audio | Global mute; buffer-size selector; volume slider; noise-gate slider |

### 3. Processor catalog and signal chain

| Group | Controls/state requiring UIA |
|---|---|
| Processor catalog | Search button; search editor/value; category rows; expand/collapse arrows; dynamic processor items; selected item; processor color action |
| Catalog groups in 1.0.2 | Sources; Filters; Sinks; Utilities; Recording, including every installed item in each category |
| Signal chain | Dynamic processor editor/card; stable processor node identity; order; selected/focused processor; drag/reorder/add/remove actions already supported by GUI |
| Editor drawers | Processor drawer expand/collapse; stream selector expand/collapse; parameter drawer controls |
| Stream selector | Stream table; table header; dynamic stream rows; current stream; expand/collapse |
| Splitter/Merger | Pipeline A; Pipeline B; selected pipeline and routing state |
| Visualizer launch | Open visualizer in window; open/select visualizer tab |
| Missing-plugin placeholder | Missing plugin name and explanatory status |

Suggested dynamic identity families are `oe.processor.<node_id>`, `oe.processor_catalog.<slug>`, and `oe.stream.<stream_id>`. They identify existing objects only; they do not add behavior.

### 4. Generic parameter editor families

Instrumenting these shared controls provides the highest coverage because core processors and Plugin API 10 plugins reuse them.

| Parameter family | Existing GUI operation/state requiring UIA |
|---|---|
| Text | Editable text/value; read-only state; commit/cancel behavior |
| Boolean/toggle | Toggle action; checked state |
| Bounded numeric | Current value; minimum; maximum; step; editable text/slider action |
| Categorical | Combo box; item list; selected item/value; expanded state |
| Selected channels | Channel chooser; dynamic channel rows/buttons; selected set |
| Mask channels | Dynamic channel-mask buttons; selected mask |
| Path | Path value; browse; clear; use default |
| Selected stream | Stream combo/list; selected stream |
| Time | Time value/editor; bounds where present |
| TTL line | Dynamic TTL-line list; selected line; none state |
| Sync line | Dynamic sync lines; set-primary action; primary-line state |

Each instance should have an Automation ID derived from the processor node and parameter key, for example `oe.processor.<node_id>.parameter.<key>`, while retaining the visible label as its accessible name.

### 5. Main utility panels and secondary windows

| Surface | Controls/state requiring UIA |
|---|---|
| Message bar/window | Timestamp label; reset timestamp; message input; send; saved-message selector; clear saved messages |
| Message Center editor | Incoming messages; outgoing messages; log/display viewports; editable message area; send |
| Console | Log text; Copy All; Clear |
| Data/visualizer tabs | Tab list; selected tab; close tab; rename editor; add tab; resizer; Move to New Window; Rename tab; Save visualizer image |
| Side panels | Info; Graph; Console; LFP; Spikes; panel selection/visibility |
| Generic data window | Window title; close/minimize/maximize; visualizer content identity |
| Filename configuration | Per-field type; enabled-state toggle; value label/editor |

### 6. Startup/default configuration dialog

| Controls/state requiring UIA |
|---|
| Configuration label and selector |
| Acquisition Board image button and label |
| File Reader image button and label |
| Neuropixels image button and label |
| Load/go button |
| Current selection, validation/error text, and dialog state |

### 7. Plugin Installer and updater

| Surface | Controls/state requiring UIA |
|---|---|
| Plugin list | List box; dynamic plugin rows; selection; installed/update/status state |
| Plugin metadata | Name; developers; version; installed version; last updated; description; dependency; status |
| Plugin actions | Download; Documentation; Uninstall; version selector |
| Filtering/sorting | Sorting label/menu; All; Installed; Updates; Filter; Source; Sink; Other |
| Auto-updater | Title; content; release notes; Download; Cancel; Don't ask again; file chooser/progress/error state |

### 8. Core processor-specific controls

| Processor | Controls/state requiring UIA |
|---|---|
| File Reader | Selected file; active stream; start time; end time; scrub drawer; zoom start/current/end; full start/min/current/end/max; zoom timeline; full timeline |
| Record Node | Directory; recording engine; record events; record spikes; channels; sync line; main sync; browse/clear/default path; stream channel monitors; FIFO drawer/status; disk monitor |
| Audio Monitor | Mute; audio output selector; Left/Both/Right; channels; spike channel |
| Audio Node/global audio | Mute; buffer size; volume; noise gate |
| Merger | Pipeline A; Pipeline B and selected input/routing state |
| Splitter | Pipeline A; Pipeline B and selected output/routing state |
| Placeholder | Missing plugin labels/status |

### 9. Bundled plugin controls

| Plugin | Controls/state requiring UIA |
|---|---|
| Arduino Output | Device; output pin; input line; gate line |
| Bandpass Filter | Low cut; high cut; channels; threads |
| Channel Map | Dynamic electrode buttons; selected/mapped state; Load PRB; Save PRB; viewport; context menu |
| Common Average Reference | Affected channels; reference channels; gain |
| LFP Viewer layout | Single; two vertical; three vertical; two horizontal; three horizontal; Sync Displays |
| LFP Viewer display | Timebase; channel height/spread; DATA/AUX/ADC channel types; voltage range; overlay events/TTL word; Pause; colour scheme; colour grouping |
| LFP Viewer thresholds | Spike raster; clip warning; saturation warning |
| LFP Viewer channels | Reverse order; sort by depth; skip; show number; dynamic channel enable buttons; dynamic event-line buttons; stream selector |
| LFP Viewer processing | Invert signal; subtract offset |
| LFP Viewer triggered display | Trigger channel; trial averaging; Reset; overlap; show/hide options; viewports |
| Phase Detector | Channel; TTL output; gate line; phase; custom Detector Interface controls/state |
| Record Control | Trigger type; edge; trigger line |
| Spike Detector main | Configure; spike-channel count; type selector; Channels; add/plus; electrode table; viewport |
| Spike Detector configuration | Threshold type uV/STD/MED; Lock; per-channel sliders; dynamic spike-channel name, waveform type, threshold type, absolute/standard-deviation/dynamic thresholds, local channels |
| Spike Viewer | Range; spike history; Clear Plots; Lock Thresholds; Invert Spikes; display size minus/plus; dynamic electrode/channel monitor/range/threshold controls; viewports |

### 10. Cross-cutting UIA properties still missing

These are requirements on the existing controls above, not new application features.

| Property/capability | Current status |
|---|---|
| Stable Automation ID namespace (`oe.*`) | Missing except three unrelated/local component IDs |
| Accessible name with correct scientific meaning | Partial/inconsistent |
| Description/help text | Partial through tooltips and command descriptions |
| Correct control role/type | Missing at runtime |
| Invoke, Toggle, ExpandCollapse, Selection, Value, RangeValue patterns | Missing at runtime |
| Enabled, visible, focused, selected, checked, expanded states | Missing at runtime |
| Numeric units, min/max/step and validation | Missing |
| Dynamic identity for nodes/streams/channels/electrodes | Missing |
| Error, progress, busy and acquisition/recording status announcements | Missing |
| Keyboard focus order and focus verification | Not exposed/tested |
| Accessible dialogs, menus, tables, lists, tabs and scroll areas | Missing |
| Windows UIA regression inventory/test | Missing |

## Minimal-change, small-PR implementation order

Every PR should preserve the official GUI behavior, contain focused tests, and be independently reviewable/releasable.

### Upstream-first contribution gate

For every portable change, contributing to `open-ephys/plugin-GUI` is preferred over maintaining a permanent fork-only patch. Follow the current official `CONTRIBUTING.md` on `development`:

1. Search official issues and open an official issue describing the bounded UI Automation problem before changing host-application code.
2. Discuss non-trivial design points with the maintainers before adding a public naming convention or changing JUCE accessibility behavior.
3. Prepare the contribution in a separate worktree based on the latest `upstream/development`, not on the fork's v1.0.2 release branch.
4. Submit one independently testable change to `open-ephys/plugin-GUI:development` from the fork.
5. Exclude Agent Native branding, fork versions, release notes, local paths, coordinate automation, and unrelated backports from the official PR.
6. Run the applicable official tests and platform build workflow, include exact commands/results, and preserve Plugin API 10 and configuration compatibility.
7. Port the accepted upstream commit back to the fork. If upstream requests a different design, follow the upstream design and document any temporary fork delta.

The official repository currently has issue templates but no pull-request template. Use the repository's current issue template and a concise PR body containing problem/issue, single change, verification, compatibility, and scope. Re-read official contribution files immediately before opening each issue or PR because upstream instructions take precedence over this inventory.

1. **UIA-000 — Official design issue.** Open one focused official issue describing the Windows UIA root-only result, desired standards-based accessibility outcome, compatibility constraints, and proposed first minimal patch. Do not present Agent Native orchestration as the upstream feature.
2. **UIA-001 — Baseline inventory test only.** Add a Windows UIA smoke/inventory test that records the current root-only tree. No product behavior change.
3. **UIA-002 — Root reachability.** TDD the smallest safe change around `MainWindow::setAccessible(false)` so descendants can be exposed; verify startup, acquisition, recording, rendering, and window behavior are unchanged.
4. **UIA-003 — Stable Automation ID bridge.** Make non-empty JUCE `ComponentID` values surface as Windows `AutomationId`, with fallback behavior unchanged.
5. **UIA-004 — Shared semantic metadata helper.** Add one small internal helper for ID, name, description, role/state metadata; no new callbacks and no Plugin API/ABI change.
6. **UIA-005 — Generic ParameterEditor families.** Cover text, toggle, numeric, categorical, channel, path, stream, time and TTL families in one shared layer, or split further if review size grows.
7. **UIA-006 — Control panel.** Acquisition, recording, destination, clock, meters and audio controls.
8. **UIA-007 — Menus and application shell.** Menus, checked/enabled state, side-panel selectors and window surfaces.
9. **UIA-008 — Processor catalog and signal chain.** Catalog/search/categories/dynamic processors, processor nodes, drawers, stream selector and routing buttons.
10. **UIA-009 — Messages, console and tabs.** Message bar/window, Message Center, console, visualizer tabs and secondary windows.
11. **UIA-010+ — Dialogs and processors.** One core processor or one bounded dialog per PR. Large surfaces such as LFP Viewer and Spike Detector should be split by sub-panel.
12. **Final parity gate.** Automated UIA inventory confirms every visible/invokable 1.0.2 control family is discoverable, stably identified, state-readable and operable without coordinate clicking.

## Acceptance rule

A control is not marked complete merely because it has a tooltip or `ComponentID`. It becomes complete only when an external Windows UIA client can:

1. find it without screen coordinates,
2. distinguish it from peer/dynamic instances,
3. read its label, meaning, value and state,
4. invoke only the behavior already present in official 1.0.2, and
5. verify the resulting state through UIA.

This rule keeps the Agent Native branch minimal: it exposes and clarifies official features; it does not invent new instrument behavior.
