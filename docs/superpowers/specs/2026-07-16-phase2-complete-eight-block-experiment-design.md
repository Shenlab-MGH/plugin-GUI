# Phase 2 Complete Eight-Block Experiment Design

## 1. Decision and goal

Phase 2 must complete the laboratory's current Open Ephys experiment under
human supervision. The run consists of eight sequential Neuropixels 2.0
`All Shanks` electrode-index blocks. Each block records all four physical
shanks simultaneously and is stored in its own Open Ephys top-level recording
directory.

The non-negotiable product goal is an end-to-end complete eight-recording run,
called the laboratory's "complete 8-shank recording" workflow. The system must
prepare, execute, monitor, stop, bind, validate, and report all eight planned
recordings in order. A single-part demo, a reusable loop that has not completed
all eight parts on the real device, a recording button wrapper, a directory
creator, an SOP, or a Source Sim-only result does not satisfy Phase 2.

Human authorization, preset confirmation when the unchanged PXI plugin cannot
provide authoritative readback, scientific QC review, and manual takeover are
parts of the complete supervised workflow. They do not permit the system to
skip a part, infer success, or end without a run-level result for every planned
recording.

Phase 2 extends the Open Ephys GUI v1.0.2 fork. It does not fork or modify the
installed Neuropixels-PXI 1.0.3-API10 plugin, imec Neuropix API 3.70.3, device
drivers, firmware, calibration data, Record Engine, Binary format, sampling,
or signal-processing algorithms.

The product version for development and supervised validation is
`0.0.2-experimental`. Promotion to `0.1.0` requires the complete acceptance
program in section 15 and human scientific review.

## 2. Scientific terminology and default coverage

NP2013 has four physical shanks and 384 simultaneous readout channels. An
`All Shanks N-M` preset selects 96 electrode indices on each of the four
physical shanks. The laboratory's current experiment uses these eight blocks:

| Part | Required default preset | Electrode-index range |
|---:|---|---:|
| 1 | `All Shanks 1-96` | 1-96 |
| 2 | `All Shanks 97-192` | 97-192 |
| 3 | `All Shanks 193-288` | 193-288 |
| 4 | `All Shanks 289-384` | 289-384 |
| 5 | `All Shanks 385-480` | 385-480 |
| 6 | `All Shanks 481-576` | 481-576 |
| 7 | `All Shanks 577-672` | 577-672 |
| 8 | `All Shanks 673-768` | 673-768 |

The user-facing workflow may retain the laboratory phrase "eight shank
recordings," but manifests, APIs, evidence, and reports must use `part_index`,
`preset`, and `electrode_range`. They must not claim that the probe has eight
physical shanks.

## 3. Design alternatives and selection

### 3.1 Rejected: pixel-only GUI replay

Coordinate replay cannot prove the selected preset, recording state, output
directory, or file integrity. DPI, focus, modal dialogs, editor layout, and
multi-monitor changes make it unsuitable for experiment completion.

### 3.2 Rejected initially: GUI and PXI dual fork

A Neuropixels-PXI fork could expose authoritative preset and hardware state,
but it expands the scientific change surface before it is demonstrated to be
necessary. Phase 2 keeps the official installed plugin unchanged.

### 3.3 Selected: GUI fork plus capability-gated official plugin interaction

The GUI fork owns the experiment state machine, original GUI recording
controls, directory preparation, authorization, audit, and recording-state
readback. The official PXI plugin remains unchanged. Existing plugin config
messages, parameter interfaces, Windows accessibility, and visual evidence
are evaluated in that order. Automatic preset selection is enabled only if a
set-and-readback contract passes device-specific acceptance tests. Otherwise
the workflow pauses for a scientist to select and confirm the preset in the
original plugin editor.

## 4. Compatibility boundary

The native Open Ephys GUI remains visible and authoritative. Phase 2 reuses:

- the existing recording parent directory selector;
- the native `Prepend`, `Main`, and `Append` filename fields;
- the native "new directory for next recording" mechanism;
- the existing ControlPanel acquisition and recording paths;
- the existing synchronization validation used by the GUI Record action;
- the existing Record Node and Binary Record Engine;
- the native `Record Node <id>/experimentN/recordingN` hierarchy.

Agent commands must enter the same validated GUI-domain operations on JUCE's
message thread. They must not call `CoreServices::setRecordingStatus(true)`,
because v1.0.2 implements that path with forced recording that can bypass the
GUI synchronization warning.

The Agent layer may refactor validation into a shared function only if parity
tests prove that the human button and typed Agent command accept and reject the
same states. The ordinary non-Agent GUI behavior remains available.

## 5. Change disclosure

Every release package must contain `OFFICIAL-DIFF.md`. Each entry includes:

- classification: `ADDED`, `MODIFIED`, or `UNCHANGED_BOUNDARY`;
- official v1.0.2 source location and fork source location;
- old behavior and new behavior;
- scientific, performance, compatibility, and recovery risks;
- automated tests and live evidence;
- feature flag or rollback procedure.

The package build fails if a changed production source file is not covered by
the disclosure registry. `OFFICIAL-DIFF.md` must explicitly state that PXI,
imec API, drivers, firmware, calibration, Record Engine, Binary format,
sampling, and signal-processing algorithms are unchanged in Phase 2.

At runtime the About/Agent panel reports:

```text
Official baseline: Open Ephys GUI 1.0.2
Fork version: 1.0.2-agent-v0.0.2-experimental
Agent mode: enabled or disabled
PXI plugin modification: none
Behavior differences: available in OFFICIAL-DIFF.md
```

## 6. Signed run manifest

No experiment workflow starts without a validated immutable run manifest. It
contains at least:

- schema version, run ID, subject/sample ID, operator, protocol ID, treatment,
  timepoint, creation time, and manifest hash;
- GUI, PXI plugin, imec API, driver, firmware, calibration, and configuration
  identities and expected hashes where available;
- OneBox and probe serials as separate fields, slot, port, dock, reference,
  signal chain, enabled streams, and Record Node IDs;
- approved experiment root and archive root with volume identity;
- an ordered list of exactly eight default parts, unless an explicitly signed
  protocol revision supplies a different list;
- per-part preset, electrode range, target duration, duration tolerance,
  maximum overrun, settling interval, and directory name;
- source/archive disk reserve, measurement cadence, maximum write rate, and
  manual takeover response interval;
- expected stream table including channel count, sample rate, event policy,
  continuity rule, timestamp policy, and synchronization requirement;
- scientist-supplied QC metrics, thresholds, units, aggregation, tool version,
  and missing-data behavior;
- scoped authorization records for prepare, acquire, record, stop, retry, and
  archive actions.

Duration is parameterized globally and may be overridden per part. The
manifest must contain concrete integer seconds and tolerances before arming;
an unresolved or implicit `2-3 minutes` value is invalid.

## 7. Independent directory contract

Each accepted part uses one predeclared, unique top-level directory. A default
parameterized name is:

```text
{run_id}_{subject}_part{part_index:02}_{electrode_start}-{electrode_end}
```

The manifest may select a laboratory template such as `M3_1`, provided all
eight resulting names are known before arming and collision-free.

Before every part, the controller must:

1. prove recording inactive and preset changes permitted;
2. canonicalize the experiment root and candidate path;
3. prove the candidate remains inside the approved root;
4. reject reserved names, separators, periods forbidden by the native filename
   fields, path-length violations, existing paths, case-folded collisions, and
   duplicate manifest names;
5. set the native parent and filename fields;
6. enable the native new-directory action;
7. read back the parent, filename fields, and computed target;
8. snapshot all Record Node counters and directory leaves;
9. write a prepared-directory audit event.

The official GUI may resolve a collision by appending ` (1)`. Agent mode must
detect the collision before recording and fail closed. It must never accept an
auto-suffixed directory as the planned part.

After recording begins, exactly one new native recording unit must be bound by
the tuple:

```text
{top_level_directory, record_node_id, experiment_number, recording_number}
```

The controller never renames or moves an accepted directory after recording.
It never renames internal experiment or recording leaves.

## 8. Experiment state machine

The run state is one of:

```text
DRAFT
PREFLIGHT
WAITING_FOR_ARM
READY_FOR_PART
WAITING_FOR_PRESET_CONFIRMATION
PART_PREPARED
ACQUISITION_STARTING
SETTLING
RECORDING_STARTING
RECORDING
RECORDING_STOPPING
VERIFYING_PART
PART_PASSED
PART_RETRY_REQUIRED
RUN_REVIEW
RUN_COMPLETE
PAUSED
FAULTED
MANUAL_TAKEOVER_REQUIRED
```

Acquisition and recording also remain independent observed state machines with
`unknown`, `inactive`, `starting`, `active`, `stopping`, and `error`. Missing
evidence never becomes `inactive`.

The normal part transition is:

```text
IDLE
-> preset selected and verified
-> directory prepared and read back
-> ACQUIRE
-> settling and synchronization verified
-> RECORD
-> target-duration watchdog
-> ACQUIRE
-> IDLE when required for the next preset
-> file closure and QC
```

The run advances only after `PART_PASSED`. A canceled, short, ambiguous,
crashed, sync-blocked, or QC-failed attempt is classified and preserved but
does not consume the planned part index.

## 9. Preset capability gate

The unchanged official PXI plugin is evaluated for these levels:

- `SEMANTIC_SET_AND_READBACK`: a typed official interface sets and returns the
  exact canonical preset and hardware application result;
- `ACCESSIBLE_SET_AND_READBACK`: UI Automation exposes a stable semantic
  control and exact selected value, corroborated by a captured plugin view;
- `HUMAN_CONFIRMED`: the Agent requests the exact preset, the scientist changes
  it in the original editor, and the Agent records screenshot evidence plus a
  scoped confirmation containing the observed value;
- `UNVERIFIABLE`: no reliable exact value is available; recording is blocked.

Pixel coordinates or image matching alone cannot elevate a preset above
`UNVERIFIABLE`. Automatic preset operation is disabled unless the first or
second level passes all eight presets in repeated device tests.

In `HUMAN_CONFIRMED`, the confirmation is bound to run ID, part, requested
preset, displayed preset text, probe identity, configuration revision,
operator, and time. Any configuration or identity change invalidates it.

## 10. Typed command and authorization contract

Mutation is not exposed through arbitrary HTTP, shell, or GUI coordinates.
Every typed command contains:

```text
run_id
command_id
idempotency_key
expected_revision
expected_mode
approval_id
requested_action
action_parameters_hash
```

The native controller rechecks dynamic safety immediately before commit and
reads back the postcondition. Duplicate command IDs with identical content
return the original result. Reuse with different content is rejected.

The command set is limited to:

- create and validate a run from a manifest;
- arm or pause the planned workflow;
- prepare the next part directory;
- request or confirm the planned preset;
- start acquisition through the validated GUI path;
- start recording through the synchronization-safe GUI path;
- stop a proven active recording;
- stop a proven active acquisition when explicitly authorized;
- classify an attempt and request a retry;
- validate a closed part;
- approve and verify a non-destructive archive copy.

No generic processor command, native HTTP forwarding, arbitrary filesystem
write, arbitrary plugin message, or arbitrary shell tool is exposed to MCP.

## 11. Synchronization and warning behavior

The GUI's synchronization gate is preserved. A typed Record request must
return `SYNC_BLOCKED` without starting recording when the same condition would
produce the GUI warning. It must not synthesize a click on "Yes."

The default policy is equivalent to selecting **No**. A synchronization
override is outside Phase 2 and cannot be granted by a one-time approval.

Before recording, every manifest-required asynchronous stream must satisfy the
approved sync policy, last-sync age, event policy, and minimum observation
window. The exact observed values enter the audit event.

## 12. Recording proof and watchdog

`mode=RECORD`, a yellow control, an advancing timer, or a new folder is each
insufficient alone. Recording becomes `active` only when all required evidence
agrees:

- ControlPanel reports the recording transition;
- callbacks are active;
- every required Record Node reports recording;
- recording timer and counters advance;
- exactly one new native unit per expected Record Node is attributable to the
  prepared top-level directory;
- filesystem payload begins changing within the bounded start interval;
- no blocking dialog, synchronization warning, disk fault, or device error is
  present.

The watchdog uses monotonic time. It records target duration, tolerance,
maximum stop deadline, poll cadence, control availability, and disk reserve.
It may stop only a proven-active recording under scoped safety-stop authority.
If recording becomes unknown, it issues no toggle and enters
`MANUAL_TAKEOVER_REQUIRED` while continuing observation.

## 13. Part verification and scientific QC

After a verified stop, the exact bound recording unit must remain unchanged in
size and write time for three checks spanning at least ten seconds. The
official Open Ephys Python tools then parse the Binary output.

Structural checks include:

- expected Record Node, experiment, recording, `settings.xml`, and
  `structure.oebin` identities;
- every manifest stream present and recording-enabled as required;
- expected channel counts and sample rates;
- duration derived from sample counts within the manifest tolerance;
- monotonic sample numbers under the approved dropout rule;
- finite and valid timestamps under the synchronization policy;
- event/TTL policy and required metadata;
- no open writer or unresolved warning.

For the observed graph, defaults are ProbeA with 384 channels at 30 kHz and
OneBox ADC with 12 channels near 30.3 kHz, but the signed manifest is
authoritative.

Structural success does not imply scientific signal-quality success. If the
manifest lacks scientist-approved signal thresholds, the part becomes
`needs_review`, not `pass`. Only a part with structural and scientific
`qc_status=pass` increments completion.

## 14. Human control and recovery

The native GUI remains usable. Human input invalidates stale Agent targeting
and requires a fresh state snapshot before automation resumes.

- `pause` prevents future planned mutations and does not stop an active
  recording;
- `stop-now` authorizes a stop only if recording is proven active;
- an unknown recording state is never toggled;
- a modal dialog, preset mismatch, directory mismatch, device disconnect,
  buffer fault, disk risk, API/UIA loss, or multiple new recording units causes
  a fail-closed pause or manual takeover;
- Open Ephys is not restarted or closed automatically after a crash or device
  fault;
- source data, logs, failed attempts, and ambiguous units are preserved.

Agent, MCP, dashboard, or network failure must not terminate Open Ephys or an
active recording. The native watchdog is the only automated component allowed
to execute a pre-authorized safety stop.

## 15. Acceptance program

The detailed Agent behavior ledger, realistic simulation scale, fault matrix,
and non-negotiable metrics are defined by
`2026-07-17-agent-behavior-audit-and-simulation-qualification.md`. That document
is a binding release gate, not optional test guidance. Section 15.7 cannot pass
when either the scientific evidence chain or Agent behavior chain is incomplete.

Phase 2 acceptance is atomic at the run level. It cannot be awarded from eight
independent partial demonstrations or from one successful part repeated only
in unit tests. One supervised run must traverse the planned sequence and
produce the complete correlated evidence set for all eight recordings.

### 15.1 Automated unit and integration tests

Tests cover every legal and illegal state transition, stale revisions,
duplicate commands, lost responses, timeout reconciliation, authorization
scope, path confinement, collisions, native auto-suffix rejection, modal
warnings, preset capability levels, manual takeover, disk thresholds, and
artifact classification.

### 15.2 Official-baseline parity

Build and run the clean official GUI v1.0.2 and the fork with Agent mode off.
Compare process startup, bundled plugins, signal-chain loading, acquisition,
recording, Binary structure, directory naming, synchronization warning, and
shutdown. Every difference must be disclosed or fixed.

### 15.3 Source Sim complete run

Run all eight parts with accelerated parameterized durations. Require eight
unique top-level directories, exact preset/part evidence, valid state
transitions, no unresolved units, parseable Binary output, complete audit, and
zero use of port 37497.

### 15.4 Fault-injection run

Inject directory collision, insufficient disk, wrong preset confirmation,
stale revision, duplicate command, MCP exit, lost native response, sync block,
manual pause, and ambiguous filesystem unit. Every case must fail closed
without corrupting accepted parts.

### 15.5 Hardware dry run

With the real official PXI plugin and device, verify identity, calibration,
all eight preset interactions, readback capability level, acquisition,
settling, synchronization, and directory preparation. No recording occurs
without a separately approved pilot manifest.

### 15.6 Supervised pilot

Run short, explicitly approved recordings for all eight parts using the same
state machine and independent-directory policy. A scientist reviews structural
and signal evidence. Any failure returns the design to experimental status.

### 15.7 Full-duration experiment acceptance

Run the actual parameterized duration plan. Completion requires:

- one end-to-end workflow invocation reaches a terminal run-level decision for
  all eight planned recordings without silently abandoning or skipping a part;
- exactly eight scientist-approved `PART_PASSED` results;
- eight unique planned top-level directories and zero auto-suffixed names;
- exact preset evidence or scoped human confirmation for every part;
- matching before/after hardware and configuration identity;
- duration, stream, channel, sample-rate, continuity, timestamp, sync, event,
  closure, and scientific QC acceptance for every part;
- zero unresolved recording units and preserved classified failed attempts;
- complete authorization and audit history;
- successful non-destructive archive verification when archive is in scope;
- explicit human review and release approval.

Only after this acceptance may the version become `0.1.0` and be described as
capable of completing the laboratory's actual complete 8-shank recording
experiment.

## 16. MCP and Skill boundary

The existing four read-only MCP tools remain unchanged. Phase 2 adds typed
planning and mutation tools only after the native state machine and approval
contracts pass tests. Tool descriptions must state required permission,
postcondition, and failure behavior. The exact allowlist is versioned.

The Open Ephys operator Skill is updated to drive manifest validation,
pre-action reporting, preset capability handling, part verification, failure
classification, and human takeover. It cannot override native safety gates or
convert `needs_review` to `pass`.

Raw electrophysiology samples are never returned through MCP. MCP transports
control results, bounded observations, artifact summaries, evidence paths, and
audit identifiers only.

## 17. Delivery slices

Phase 2 is delivered as independently reviewable slices:

1. disclosure registry and official parity harness;
2. manifest schema and deterministic experiment state machine;
3. safe native directory preparation and authoritative readback;
4. synchronization-safe acquisition and recording commands;
5. preset capability probe and human-confirmed fallback;
6. watchdog, pause, takeover, and failure classification;
7. Binary structural verifier and scientist QC boundary;
8. exact MCP tools, Skill workflow, and operator reporting;
9. Source Sim, fault injection, hardware pilot, and full experiment evidence;
10. signed `0.1.0` release package after human approval.

No slice may claim complete-experiment capability before section 15.7 passes.
