# Phase 2 Complete Eight-Block Experiment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and validate one supervised workflow that completes all eight laboratory shank-recording parts, each with the exact planned `All Shanks` preset, a unique native top-level folder, parameterized duration, verified stop, artifact QC, and a run-level result.

**Architecture:** Keep Open Ephys GUI v1.0.2 and the official Neuropixels-PXI 1.0.3 plugin as the acquisition engine. Add fail-closed typed operations to the GUI fork, keep the watchdog and recording-state protection in-process, and place manifest orchestration, audit, artifact verification, MCP tools, and the Skill above the authenticated loopback endpoint. Automatic preset control is capability-gated; the unchanged PXI plugin falls back to bound human confirmation when exact semantic readback is unavailable.

**Tech Stack:** C++17, JUCE, cpp-httplib, CMake/MSVC, Python 3.12, Pydantic 2, official MCP Python SDK, pytest, PowerShell 7, official `open-ephys-python-tools`, JSON/JSONL, SHA-256.

## Global Constraints

- Official GUI baseline is tag `v1.0.2`, commit `c91afebcfb0678a667fb93f6312ed33c56ec640f`.
- The official Neuropixels-PXI `1.0.3-API10` plugin is not modified in Phase 2.
- Do not modify Record Engine, Binary format, sample transport, signal processing, imec API, drivers, firmware, or calibration.
- Do not use native HTTP port `37497` for Agent control.
- Every accepted part has one planned, unique top-level folder; auto-suffixed ` (1)` names are rejected.
- Default coverage is the ordered eight presets from `All Shanks 1-96` through `All Shanks 673-768`.
- Durations, tolerances, naming, disk reserve, sync policy, and QC criteria are concrete manifest parameters.
- Pixel coordinates or image matching alone never prove preset, acquisition, recording, directory, or artifact state.
- Synchronization warnings block recording; Phase 2 has no override.
- An unknown state is never toggled.
- A failed or ambiguous attempt is preserved and does not advance the part index.
- Full completion requires one real-device supervised run with eight `PART_PASSED` results and human release approval.
- Every production change relative to official v1.0.2 must appear in `OFFICIAL-DIFF.md` with tests, risk, and rollback.

---

### Task 1: Official-difference registry and package gate

**Files:**
- Create: `OFFICIAL-DIFF.md`
- Create: `tools/windows/Test-OfficialDiffCoverage.ps1`
- Create: `Tests/AgentContracts/test_official_diff_coverage_source.py`
- Modify: `tools/windows/Invoke-AgentChecks.ps1`
- Modify: `tools/windows/New-AgentRuntimePackage.ps1`

**Interfaces:**
- Consumes: `git diff --name-only c91afebcfb0678a667fb93f6312ed33c56ec640f...HEAD`.
- Produces: `Test-OfficialDiffCoverage.ps1 -Repository <path>` with exit `0` only when every changed production source has a registry entry.

- [ ] **Step 1: Write the failing source-contract test**

```python
def test_release_requires_official_diff_coverage():
    package = read("tools/windows/New-AgentRuntimePackage.ps1")
    checks = read("tools/windows/Invoke-AgentChecks.ps1")
    registry = read("OFFICIAL-DIFF.md")
    assert "Test-OfficialDiffCoverage.ps1" in package
    assert "Test-OfficialDiffCoverage.ps1" in checks
    assert "PXI plugin modification | NONE" in registry
    assert "Record Engine | UNCHANGED_BOUNDARY" in registry
```

- [ ] **Step 2: Run it and verify RED**

Run: `python Tests/AgentContracts/test_official_diff_coverage_source.py`

Expected: FAIL because the registry and coverage gate do not exist.

- [ ] **Step 3: Add the registry and deterministic coverage script**

The registry uses one row per changed production path:

```markdown
| Path | Class | Official behavior | Fork behavior | Risk | Verification | Rollback |
|---|---|---|---|---|---|---|
| Source/Agent/ | ADDED | No Agent subsystem | Authenticated Agent subsystem | New control surface | AgentCore tests | omit Agent mode flags |
| Neuropixels-PXI plugin | NONE | Official 1.0.3-API10 | Unchanged | None introduced | installed plugin hash | use official install |
| Record Engine | UNCHANGED_BOUNDARY | Native writer | Unchanged | None introduced | baseline Binary comparison | use official GUI |
```

The PowerShell script parses `| <path> |` entries, compares them with changed
files under `Source/`, `Plugins/`, `CMakeLists.txt`, and `JuceLibraryCode/`, and
fails with `UNDISCLOSED_CHANGE:<path>` for the first uncovered path.

- [ ] **Step 4: Run contract and complete local checks**

Run: `python Tests/AgentContracts/test_official_diff_coverage_source.py`

Run: `pwsh -NoProfile -File tools/windows/Invoke-AgentChecks.ps1`

Expected: both PASS; the package script invokes the coverage gate before copying files.

- [ ] **Step 5: Commit**

```powershell
git add OFFICIAL-DIFF.md tools/windows Tests/AgentContracts
git commit -m "build: require official behavior disclosure"
```

### Task 2: Strict run manifest and eight-part naming plan

**Files:**
- Create: `integrations/mcp/src/open_ephys_agent_mcp/experiment/__init__.py`
- Create: `integrations/mcp/src/open_ephys_agent_mcp/experiment/manifest.py`
- Create: `integrations/mcp/tests/experiment/test_manifest.py`
- Create: `integrations/skills/open-ephys-operator/references/run-manifest-v2.schema.json`
- Modify: `integrations/skills/open-ephys-operator/scripts/validate-session-manifest.py`

**Interfaces:**
- Produces: `RunManifest.model_validate_json(data: str) -> RunManifest`.
- Produces: `RunManifest.planned_directory(part_index: int) -> pathlib.PurePath`.
- Produces: `PartPlan(part_index, preset, electrode_start, electrode_end, target_duration_seconds, duration_tolerance_seconds, maximum_overrun_seconds, directory_name)`.

- [ ] **Step 1: Write failing manifest tests**

```python
def test_default_actual_run_has_exactly_eight_ordered_parts():
    manifest = RunManifest.model_validate(valid_manifest())
    assert [p.preset for p in manifest.parts] == [
        "All Shanks 1-96", "All Shanks 97-192",
        "All Shanks 193-288", "All Shanks 289-384",
        "All Shanks 385-480", "All Shanks 481-576",
        "All Shanks 577-672", "All Shanks 673-768",
    ]

def test_rejects_duplicate_or_colliding_directory_names():
    payload = valid_manifest()
    payload["parts"][1]["directory_name"] = payload["parts"][0]["directory_name"]
    with pytest.raises(ValidationError, match="DUPLICATE_PART_DIRECTORY"):
        RunManifest.model_validate(payload)

def test_duration_is_concrete_and_parameterized():
    payload = valid_manifest()
    payload["parts"][3]["target_duration_seconds"] = 150
    manifest = RunManifest.model_validate(payload)
    assert manifest.parts[3].target_duration_seconds == 150
```

- [ ] **Step 2: Run and verify RED**

Run: `py -3.12 -m uv run --frozen --project integrations/mcp pytest integrations/mcp/tests/experiment/test_manifest.py -q`

Expected: import failure for the missing `experiment.manifest` module.

- [ ] **Step 3: Implement strict Pydantic models**

Use `ConfigDict(extra="forbid", frozen=True)`. Validate literal loopback-independent
data only. Enforce eight unique `part_index` values `1..8`, exact default preset
order unless `protocol_revision.approved_override` is present, positive integer
durations, nonnegative bounded tolerances, absolute experiment root, relative
single-segment directory names, Windows reserved-name rejection, and case-folded
uniqueness.

```python
class PartPlan(BaseModel):
    model_config = ConfigDict(extra="forbid", frozen=True)
    part_index: int = Field(ge=1, le=8)
    preset: str = Field(min_length=1, max_length=128)
    electrode_start: int = Field(ge=1)
    electrode_end: int = Field(ge=1)
    target_duration_seconds: int = Field(ge=1, le=3600)
    duration_tolerance_seconds: int = Field(ge=0, le=60)
    maximum_overrun_seconds: int = Field(ge=1, le=300)
    directory_name: str = Field(min_length=1, max_length=120)
```

- [ ] **Step 4: Make the Skill validator delegate to the same schema rules**

Keep the command-line validator read-only. It returns `0` only for a complete
manifest and prints stable codes such as `PART_COUNT_NOT_EIGHT`,
`DUPLICATE_PART_DIRECTORY`, and `DURATION_NOT_CONCRETE`.

- [ ] **Step 5: Run all MCP and Skill tests**

Run: `pwsh -NoProfile -File tools/windows/Invoke-OpenEphysAgentMcpTests.ps1`

Expected: all tests PASS.

- [ ] **Step 6: Commit**

```powershell
git add integrations/mcp integrations/skills/open-ephys-operator
git commit -m "feat: validate complete eight-part run manifests"
```

### Task 3: Deterministic run and part state machine

**Files:**
- Create: `integrations/mcp/src/open_ephys_agent_mcp/experiment/state.py`
- Create: `integrations/mcp/src/open_ephys_agent_mcp/experiment/events.py`
- Create: `integrations/mcp/tests/experiment/test_state.py`

**Interfaces:**
- Produces: `RunState`, `PartState`, `RunEvent`, `RunSnapshot`.
- Produces: `ExperimentReducer.apply(snapshot: RunSnapshot, event: RunEvent) -> RunSnapshot`.
- Consumes: immutable `RunManifest` and event sequence.

- [ ] **Step 1: Write failing reducer tests**

```python
def test_run_cannot_complete_until_all_eight_parts_pass():
    snapshot = armed_snapshot()
    for index in range(1, 8):
        snapshot = pass_part(snapshot, index)
    with pytest.raises(TransitionRejected, match="PARTS_INCOMPLETE"):
        ExperimentReducer.apply(snapshot, RunEvent.run_review_approved())

def test_failed_attempt_does_not_advance_part():
    snapshot = recording_snapshot(part_index=3, attempt=1)
    next_state = ExperimentReducer.apply(snapshot, RunEvent.qc_failed("DROPOUT"))
    assert next_state.planned_part == 3
    assert next_state.completed_parts == (1, 2)
    assert next_state.classified_nonpart_attempts[-1].attempt == 1

def test_pause_never_implies_stop():
    snapshot = recording_snapshot(part_index=1, attempt=1)
    paused = ExperimentReducer.apply(snapshot, RunEvent.pause_requested())
    assert paused.run_state is RunState.PAUSED
    assert paused.recording_state is RecordingState.ACTIVE
```

- [ ] **Step 2: Verify RED**

Run: `py -3.12 -m uv run --frozen --project integrations/mcp pytest integrations/mcp/tests/experiment/test_state.py -q`

Expected: missing reducer module.

- [ ] **Step 3: Implement a pure event reducer**

The reducer performs no I/O or time reads. Every event carries its observation
time and evidence IDs. It rejects illegal transitions with stable error codes.
`RUN_COMPLETE` requires parts `(1,2,3,4,5,6,7,8)`, zero unresolved units, and
run review approval.

- [ ] **Step 4: Add property-style transition matrix coverage**

Enumerate every `RunState x RunEvent.kind` pair and assert it either produces a
documented state or a `TransitionRejected` code. Assert replaying the same event
ID is idempotent and conflicting reuse is rejected.

- [ ] **Step 5: Run and commit**

Run: `py -3.12 -m uv run --frozen --project integrations/mcp pytest integrations/mcp/tests/experiment -q`

```powershell
git add integrations/mcp
git commit -m "feat: add complete experiment state machine"
```

### Task 4: Native independent-directory policy

**Files:**
- Create: `Source/Agent/AgentExperimentDirectory.h`
- Create: `Source/Agent/AgentExperimentDirectory.cpp`
- Create: `Tests/AgentCore/AgentExperimentDirectoryTests.cpp`
- Modify: `Source/Agent/CMakeLists.txt`
- Modify: `Tests/AgentCore/CMakeLists.txt`

**Interfaces:**
- Produces: `AgentDirectoryRequest { approvedRoot, directoryName, expectedRevision }`.
- Produces: `AgentDirectoryDecision AgentExperimentDirectory::validate(const AgentDirectoryRequest&, const AgentDirectoryObservation&)`.
- Produces stable reasons: `outsideApprovedRoot`, `alreadyExists`, `invalidNativeName`, `caseCollision`, `recordingNotInactive`, `revisionConflict`, `ready`.

- [ ] **Step 1: Add the failing C++ tests**

```cpp
require (policy.validate ({ root, "run_M3_part01_1-96", 7 },
                          { false, {}, AgentObservedMode::idle, 7 }).outcome
             == AgentDirectoryOutcome::ready,
         "new confined IDLE directory must be ready");
require (policy.validate ({ root, "run_M3_part01_1-96", 7 },
                          { true, {}, AgentObservedMode::idle, 7 }).outcome
             == AgentDirectoryOutcome::alreadyExists,
         "existing directory must fail closed");
require (policy.validate ({ root, "..\\escape", 7 },
                          { false, {}, AgentObservedMode::idle, 7 }).outcome
             == AgentDirectoryOutcome::invalidNativeName,
         "path separators must be rejected");
```

- [ ] **Step 2: Configure/build and verify RED**

Run: `cmake --build Build-agent-core --config Release --target AgentExperimentDirectoryTests`

Expected: target or source does not exist.

- [ ] **Step 3: Implement the pure policy**

Use `std::filesystem::path` lexical normalization and Windows case-insensitive
comparison. Accept a single native filename segment with no slash, backslash,
period, control character, trailing space, trailing period, device name, or
auto-suffix. Require observed `IDLE` and matching revision.

- [ ] **Step 4: Run native tests and commit**

Run: `pwsh -NoProfile -File tools/windows/Invoke-AgentCoreTests.ps1`

```powershell
git add Source/Agent Tests/AgentCore
git commit -m "feat: validate independent recording directories"
```

### Task 5: ControlPanel directory adapter and readback endpoint

**Files:**
- Create: `Source/Agent/ControlPanelExperimentAdapter.h`
- Create: `Source/Agent/ControlPanelExperimentAdapter.cpp`
- Create: `Tests/AgentContracts/test_control_panel_experiment_adapter_source.py`
- Modify: `Source/Agent/AgentLoopbackServer.h`
- Modify: `Source/Agent/AgentLoopbackServer.cpp`
- Modify: `Source/Agent/AgentControlProtocol.h`
- Modify: `Source/Agent/AgentControlProtocol.cpp`
- Modify: `Source/UI/ControlPanel.h`
- Modify: `Source/UI/ControlPanel.cpp`
- Modify: `Source/Agent/CMakeLists.txt`

**Interfaces:**
- Produces authenticated `GET /v1/experiment/directory` readback.
- Produces authenticated, armed `PUT /v1/experiment/directory` with `run_id`, `command_id`, `expected_revision`, `approved_root`, and `directory_name`.
- Uses existing native parent/name/new-directory behavior; it does not create a replacement writer path.

- [ ] **Step 1: Write failing protocol and source-contract tests**

Assert that the request rejects unknown fields and more than 16 KiB, executes
only on the JUCE message thread, calls named ControlPanel methods, reads the
computed name back, and returns `DIRECTORY_COLLISION` before Record Node can
append ` (1)`.

```python
assert "getAgentRecordingDirectorySnapshot" in control_panel
assert "prepareAgentRecordingDirectory" in control_panel
assert 'Put ("/v1/experiment/directory"' in server
assert "CoreServices::setRecordingStatus" not in adapter
```

- [ ] **Step 2: Verify RED**

Run: `python Tests/AgentContracts/test_control_panel_experiment_adapter_source.py`

Expected: FAIL for missing adapter and routes.

- [ ] **Step 3: Add minimal ControlPanel semantic methods**

`prepareAgentRecordingDirectory` must set the original parent and filename
fields, request the original new-directory behavior, recompute the native name,
and return all values. It rejects active/unknown recording and never touches
RecordThread or RecordEngine directly.

- [ ] **Step 4: Add strict protocol parsing and readback**

The response contains canonical root, native filename field values, computed
target, existence/collision state, ControlPanel revision, and current mode. It
contains no token and no arbitrary filesystem content.

- [ ] **Step 5: Build, run checks, and perform a no-record live readback test**

Run: `cmake --build Build --config Release --target Agent_tests`

Run: `pwsh -NoProfile -File tools/windows/Invoke-AgentChecks.ps1`

Launch an isolated fork without hardware, prepare a temporary directory while
IDLE, verify the GUI filename controls show the same value, and close normally
without acquisition or recording.

- [ ] **Step 6: Commit**

```powershell
git add Source Tests docs/agent/evidence
git commit -m "feat: prepare native per-part recording directories"
```

### Task 6: Synchronization-safe acquisition and recording commands

**Files:**
- Create: `Source/Agent/AgentRecordingSafety.h`
- Create: `Source/Agent/AgentRecordingSafety.cpp`
- Create: `Tests/AgentCore/AgentRecordingSafetyTests.cpp`
- Modify: `Source/UI/ControlPanel.h`
- Modify: `Source/UI/ControlPanel.cpp`
- Modify: `Source/Agent/AgentCommand.h`
- Modify: `Source/Agent/AgentTransportCoordinator.cpp`
- Modify: `Source/Agent/AgentControlProtocol.cpp`
- Modify: `Source/Agent/AgentLoopbackServer.cpp`
- Modify: `Source/Agent/CMakeLists.txt`
- Modify: `Tests/AgentCore/CMakeLists.txt`

**Interfaces:**
- Produces `ControlPanel::requestValidatedRecordingStart(origin, context)`.
- Produces `AgentRecordingGateDecision` with `ready`, `syncBlocked`, `directoryNotPrepared`, `recordNodesNotReady`, `stateUnknown`, or `revisionConflict`.
- Preserves existing `POST /v1/transport/requests` with expanded authorization fields.

- [ ] **Step 1: Write failing parity and safety tests**

```cpp
require (gate.evaluate (syncedReadyObservation()).outcome
             == AgentRecordingGateOutcome::ready,
         "ready synchronized state must pass");
require (gate.evaluate (unsynchronizedObservation()).outcome
             == AgentRecordingGateOutcome::syncBlocked,
         "unsynchronized streams must block without override");
require (! unsynchronizedObservation().recordCommandIssued,
         "sync rejection must not issue a record command");
```

Add a source contract proving Agent code never calls forced
`CoreServices::setRecordingStatus(true)`.

- [ ] **Step 2: Verify RED**

Run the new target and confirm missing types fail compilation.

- [ ] **Step 3: Extract one shared GUI validation result**

Refactor the human Record action and Agent request to call the same pure
validation routine. Human mode may display the existing warning; Agent mode
returns `SYNC_BLOCKED` and behaves like choosing **No**. There is no override
field in the protocol.

- [ ] **Step 4: Enforce command identity and scoped approval**

Extend request parsing to require `run_id`, `command_id`, `idempotency_key`,
`expected_revision`, `expected_mode`, `approval_id`, and
`action_parameters_hash`. Reject missing/unknown fields and content conflicts.

- [ ] **Step 5: Run all native tests and safe live Source Sim transitions**

Use parameterized short durations only in the signed Source Sim manifest.
Verify `IDLE -> ACQUIRE -> RECORD -> ACQUIRE -> IDLE`, state readback after each
step, and no 37497 listener.

- [ ] **Step 6: Commit**

```powershell
git add Source Tests docs/agent/evidence
git commit -m "feat: add synchronization-safe recording control"
```

### Task 7: Preset capability gate and bound human confirmation

**Files:**
- Create: `integrations/mcp/src/open_ephys_agent_mcp/experiment/preset.py`
- Create: `integrations/mcp/tests/experiment/test_preset.py`
- Create: `tools/windows/Test-NeuropixelsPresetCapability.ps1`
- Create: `Tests/AgentContracts/test_preset_capability_probe_source.py`

**Interfaces:**
- Produces `PresetCapability = SEMANTIC_SET_AND_READBACK | ACCESSIBLE_SET_AND_READBACK | HUMAN_CONFIRMED | UNVERIFIABLE`.
- Produces `PresetConfirmation` bound to run, part, preset, probe, configuration revision, evidence path, operator, and time.

- [ ] **Step 1: Write failing capability tests**

```python
def test_pixel_only_observation_is_unverifiable():
    result = classify_preset_capability(PresetEvidence(pixel_match="97-192"))
    assert result is PresetCapability.UNVERIFIABLE

def test_human_confirmation_is_invalidated_by_probe_change():
    confirmation = confirmed_preset(probe_serial="23299804124")
    with pytest.raises(PresetConfirmationError, match="PROBE_IDENTITY_CHANGED"):
        confirmation.verify(current_probe_serial="23409412544")
```

- [ ] **Step 2: Verify RED**

Run the focused pytest file and confirm the module is missing.

- [ ] **Step 3: Implement the capability classifier and confirmation model**

Reject screenshots without exact text and human identity. Hash the evidence
file and confirmation payload. A changed preset, probe, configuration revision,
run ID, or part invalidates confirmation.

- [ ] **Step 4: Implement the read-only official-plugin capability probe**

The PowerShell probe performs no preset change. It records UIA tree, exact
displayed preset text if available, installed plugin version/hash, and whether
a documented semantic readback exists. It never claims SET capability from a
read-only probe.

- [ ] **Step 5: Run tests and commit**

```powershell
git add integrations/mcp tools/windows Tests/AgentContracts
git commit -m "feat: gate preset automation on verified readback"
```

### Task 8: Native watchdog, pause, and takeover semantics

**Files:**
- Create: `Source/Agent/AgentRecordingWatchdog.h`
- Create: `Source/Agent/AgentRecordingWatchdog.cpp`
- Create: `Tests/AgentCore/AgentRecordingWatchdogTests.cpp`
- Modify: `Source/Agent/AgentLoopbackServer.cpp`
- Modify: `Source/Agent/CMakeLists.txt`
- Modify: `Tests/AgentCore/CMakeLists.txt`

**Interfaces:**
- Produces `AgentWatchdogPlan { targetDuration, tolerance, maximumOverrun, pollCadence, safetyStopAuthorized }`.
- Produces `AgentWatchdogDecision = observe | requestVerifiedStop | requireManualTakeover | complete`.
- Uses monotonic time supplied through a testable clock interface.

- [ ] **Step 1: Write failing watchdog tests**

```cpp
require (watchdog.tick (activeAtDeadline()).decision
             == AgentWatchdogDecision::requestVerifiedStop,
         "proven active recording with authority must stop at deadline");
require (watchdog.tick (unknownAtDeadline()).decision
             == AgentWatchdogDecision::requireManualTakeover,
         "unknown state must never toggle");
require (watchdog.pause (activeRecording()).recordingAction
             == AgentRecordingAction::none,
         "pause must not stop active recording");
```

- [ ] **Step 2: Verify RED, implement minimal pure watchdog, and verify GREEN**

The watchdog never performs filesystem I/O or GUI operations. It returns a
decision consumed on the message thread. A verified stop still requires state
readback and transitions to manual takeover on ambiguity.

- [ ] **Step 3: Add endpoint status and bounded audit events**

Expose target, deadline, current decision, control availability, and redacted
approval ID. Do not expose the bearer token or raw samples.

- [ ] **Step 4: Run AgentCore and fault-injection tests, then commit**

```powershell
git add Source/Agent Tests/AgentCore
git commit -m "feat: add native recording watchdog and takeover"
```

### Task 9: Exact recording-unit binding and Binary QC

**Files:**
- Create: `integrations/mcp/src/open_ephys_agent_mcp/experiment/artifacts.py`
- Create: `integrations/mcp/src/open_ephys_agent_mcp/experiment/qc.py`
- Create: `integrations/mcp/tests/experiment/test_artifacts.py`
- Create: `integrations/mcp/tests/experiment/test_qc.py`
- Modify: `integrations/mcp/pyproject.toml`
- Modify: `integrations/mcp/uv.lock`

**Interfaces:**
- Produces `RecordingUnitIdentity(top_level, record_node_id, experiment_number, recording_number)`.
- Produces `bind_recording_unit(before, after, planned_root) -> RecordingUnitIdentity`.
- Produces `verify_closed_unit(unit, stream_expectations, duration_plan) -> StructuralQcReport`.

- [ ] **Step 1: Add failing binding tests**

```python
def test_binds_one_new_unit_inside_the_planned_root():
    unit = bind_recording_unit(before_snapshot(), after_with_one_new_unit(), root)
    assert unit.recording_number == 1

def test_rejects_newest_folder_heuristic_and_multiple_candidates():
    with pytest.raises(UnitBindingError, match="MULTIPLE_NEW_UNITS"):
        bind_recording_unit(before_snapshot(), after_with_two_new_units(), root)
```

- [ ] **Step 2: Verify RED and implement deterministic set-difference binding**

Never sort by Explorer time. Require the planned top-level root, expected Record
Node IDs, counter correlation, and one attributable unit per expected node.

- [ ] **Step 3: Pin the official Python loader and add failing fixture tests**

Pin official `open-ephys-python-tools` tag `v1.0.1`, peeled commit
`4032c290bbfde85320dc95ab53e83251cdae7edb`, in `uv.lock`. Add a small
generated Binary fixture and test channels, sample rates, sample counts,
monotonic sample numbers, finite timestamps, event policy, and stable closure.

- [ ] **Step 4: Implement structural QC and the scientific-review boundary**

`StructuralQcReport` may be `pass`, `fail`, or `needs_review`. It cannot emit
scientific `pass` when the manifest lacks versioned signal thresholds.

- [ ] **Step 5: Run tests and commit**

```powershell
git add integrations/mcp
git commit -m "feat: bind and verify native recording artifacts"
```

### Task 9A: Durable Agent behavior ledger and replay authority

**Files:**
- Create: `integrations/mcp/src/open_ephys_agent_mcp/experiment/action_audit.py`
- Create: `integrations/mcp/tests/experiment/test_action_audit.py`
- Create: `tools/agent_audit/`
- Create: `Tests/AgentAudit/`
- Create: `docs/agent/action-audit-v0.0.1.md`
- Modify: `tools/agent_gateway/open_ephys_agent_gateway/audit.py`

**Interfaces:**
- Produces a unified, versioned action envelope and tamper-evident chain.
- Produces a single-writer SQLite WAL collector, artifact CAS, checkpoint/seal,
  offline verifier, and dry-run reference replay.
- Correlates Agent, human, MCP, native endpoint, UIA, GUI input, video, and
  filesystem evidence without storing secrets or raw neural data.

- [x] **Step 1: Add failing action-envelope and chain-verifier tests**

Cover correlation, hash tampering, sequence gaps, recursive secret redaction,
human takeover, and mutation intent/readback completeness.

- [x] **Step 2: Implement the minimal in-memory builder and verifier**

This core is not a durable operational ledger and must not be described as one.

- [ ] **Step 3: TDD the authoritative SQLite WAL collector and artifact CAS**

Intent commit must precede mutation. Add crash cuts for write/flush/fsync/ack,
single-writer sequence allocation, DPAPI/CNG checkpoint protection, atomic
artifact publication, sealing, and offline verification.

- [ ] **Step 4: Integrate producers and enforce fail-closed mutation**

Connect the coordinator, gateway, native endpoint, UIA recorder, human takeover,
window capture, and artifact binder. Audit degradation blocks new mutation but
does not block an authorized emergency stop.

- [ ] **Step 5: Implement deterministic dry-run replay**

Compile only fully verified semantic correlations. Do not replay coordinates,
secrets, approvals, PIDs, HWNDs, or hidden model reasoning.

- [ ] **Step 6: Execute the Agent-audit PR gate and commit**

Run the exact PR scale in the binding qualification specification and retain
seeded evidence.

### Task 10: Experiment coordinator, exact MCP tools, and Skill

**Files:**
- Create: `integrations/mcp/src/open_ephys_agent_mcp/experiment/coordinator.py`
- Create: `integrations/mcp/tests/experiment/test_coordinator.py`
- Modify: `integrations/mcp/src/open_ephys_agent_mcp/native_client.py`
- Modify: `integrations/mcp/src/open_ephys_agent_mcp/service.py`
- Modify: `integrations/mcp/src/open_ephys_agent_mcp/server.py`
- Modify: `integrations/mcp/tests/test_mcp_protocol.py`
- Modify: `integrations/skills/open-ephys-operator/SKILL.md`
- Modify: `integrations/skills/open-ephys-operator/references/eight-shank-sop.md`
- Modify: `integrations/skills/open-ephys-operator/references/tool-workflows.md`

**Interfaces:**
- Produces persistent append-only run events and immutable `RunSnapshot`.
- Adds exact typed MCP tools: `oe_create_run`, `oe_get_run`, `oe_arm_run`, `oe_prepare_next_part`, `oe_confirm_preset`, `oe_start_part`, `oe_stop_active_part`, `oe_verify_part`, `oe_pause_run`, and `oe_get_run_report`.
- Keeps the four v0.0.1 read-only tools unchanged.

- [ ] **Step 1: Write failing coordinator tests**

Test an eight-part accelerated fake-native run, retry of failed part 4, crash
replay from JSONL, pause during recording without stop, stale revision, and
MCP loss while the native watchdog remains active.

```python
assert report.result == "RUN_COMPLETE"
assert report.completed_parts == tuple(range(1, 9))
assert len(report.accepted_directories) == 8
assert report.unresolved_recording_units == ()
```

- [ ] **Step 2: Verify RED and implement the event-sourced coordinator**

Persist an audit intent before mutation and the readback result after mutation.
On restart, replay events, fetch fresh native state, and enter manual takeover
if it conflicts with the last snapshot.

- [ ] **Step 3: Add strict native-client methods and MCP schemas**

Every mutation tool uses explicit input models with `extra="forbid"`. Tool
annotations mark mutations accurately. Server instructions state that no tool
can override sync, unknown-state, directory, or QC gates.

- [ ] **Step 4: Update the Skill using the official skill validator**

The Skill runs identity, manifest, preflight, authorization, preset capability,
directory, transport, binding, QC, and run-review checkpoints in order. It
forbids skipping a part and forbids claiming complete before eight passed parts.

- [ ] **Step 5: Run protocol, Skill, and restart tests**

Run: `pwsh -NoProfile -File tools/windows/Invoke-OpenEphysAgentMcpTests.ps1`

Run: `py -3.12 C:/Users/Sshen/.codex/skills/.system/skill-creator/scripts/quick_validate.py integrations/skills/open-ephys-operator`

- [ ] **Step 6: Commit**

```powershell
git add integrations/mcp integrations/skills/open-ephys-operator
git commit -m "feat: orchestrate complete eight-part experiments"
```

### Task 11: Full-run acceptance harness and signed package

**Files:**
- Create: `tools/windows/Invoke-EightPartExperimentAcceptance.ps1`
- Create: `Tests/AgentContracts/test_eight_part_acceptance_source.py`
- Create: `docs/agent/phase2-acceptance-contract.md`
- Modify: `tools/windows/Invoke-AgentChecks.ps1`
- Modify: `tools/windows/New-AgentRuntimePackage.ps1`

**Interfaces:**
- Produces `EIGHT-PART-ACCEPTANCE.json` with per-part evidence, package hashes, no secrets, and terminal result.
- Produces accelerated Source Sim mode and real-device supervised mode from the same manifest schema.

- [ ] **Step 1: Write the failing acceptance source contract**

Require the harness to assert exact parts `1..8`, eight unique directories,
zero ` (N)` suffixes, state transition evidence, preset evidence, duration/QC,
zero unresolved units, zero port 37497 listeners, manifest hashes, no token,
and normal process closure.

- [ ] **Step 2: Verify RED, then implement dry-run and Source Sim harnesses**

The harness has no default mutation mode. It requires `-Manifest`,
`-MaintenanceWindowApproved`, and either `-SourceSim` or
`-RealDeviceSupervised`. Real-device mode additionally requires a short-lived
arm file whose hash matches the manifest authorization record.

- [ ] **Step 3: Run the complete accelerated eight-part Source Sim acceptance**

Expected: one invocation produces eight independent directories, eight passed
structural fixtures, a complete event log, and `result=PASS`.

- [ ] **Step 4: Run the fault matrix**

Inject collision, wrong preset confirmation, disk reserve failure, sync block,
lost response, stale revision, process pause, MCP exit, and two new filesystem
units. Expected: every run fails closed and preserves prior accepted parts.

- [ ] **Step 5: Package and verify every manifest hash**

Package must contain `OFFICIAL-DIFF.md`, experiment schemas, Skill, MCP,
acceptance harness, fork executable, and rollback instructions. Recompute every
SHA-256 and require `source_dirty=false`.

- [ ] **Step 6: Execute hardware dry run, supervised pilot, and full experiment**

These three actions require separate scientist-approved manifests. Do not merge
their authorization. Record exact device identities and preset capability.
Only the full-duration run with eight scientist-approved parts may set
`complete_experiment_capability=true`.

- [ ] **Step 7: Final verification and commit**

Run: `pwsh -NoProfile -File tools/windows/Invoke-AgentChecks.ps1`

Run: `git diff --check`

Run: `git status --porcelain=v1`

Expected: all checks PASS and worktree is clean after the evidence commit.

```powershell
git add tools Tests docs OFFICIAL-DIFF.md
git commit -m "test: prove complete eight-part experiment workflow"
```

### Task 12: Large-scale realistic simulation qualification

**Files:**
- Create: `tools/windows/Invoke-AgentQualification.ps1`
- Create: `integrations/mcp/src/open_ephys_agent_mcp/experiment/qualification.py`
- Create: `integrations/mcp/tests/experiment/test_qualification.py`
- Create: `docs/agent/qualification-profile-v0.0.1.json`
- Create: `docs/agent/evidence/qualification/`

- [ ] **Step 1: Encode the binding PR/nightly/release profiles**

Profiles may be changed only through reviewed version control. Counts can be
raised; lowering a release gate requires explicit scientist approval and a
documented risk decision.

- [ ] **Step 2: TDD the independent reference model and report evaluator**

The evaluator refuses PASS for insufficient sample count, missing seed/hash,
flaky rerun, behavior-chain gap, artifact mismatch, unsafe transition, secret
leak, incomplete part, or missing real-process evidence.

- [ ] **Step 3: Execute PR and nightly synthetic/fault gates**

Retain the first failure and deterministic reproduction command. Do not hide a
failure with retries.

- [ ] **Step 4: Execute real-process Source Sim gates**

Run 30 accelerated and 10 nominal-duration complete eight-part cycles using
the packaged fork, real Windows message thread, real Binary engine, actual
isolated disk output, and pinned official file loader.

- [ ] **Step 5: Execute durability and capture soak gates**

Require a 24-hour journal/replay soak, four-hour window capture soak, 100
collector/replayer restarts, and zero silent evidence loss.

- [ ] **Step 6: Execute supervised real-device qualification**

Requires three separate authorized, consecutive 8/8 qualification runs and
scientist review. This step cannot be simulated or self-approved by the Agent.

- [ ] **Step 7: Seal and review qualification evidence**

Produce one signed report covering executable/config/plugin hashes, all seeds,
first-failure records, behavior-ledger terminal hashes, artifact roots, per-part
QC, and explicit human approval.

## Execution order and release gates

Tasks execute in order. Tasks 1-4 may run without Open Ephys. Task 5 permits
only IDLE directory tests. Task 6 first permits Source Sim acquisition and
recording. Task 7 does not mutate the official PXI plugin. Tasks 8-10 remain
experimental until the Source Sim and fault matrix pass. Task 11 and Task 12
real-device steps require separate human authorization at each gate. The
binding qualification specification must pass in addition to Task 11; a single
successful demonstration never substitutes for the required population tests.

No implementation checkpoint, demo, or partial package changes the final goal:
one supervised, end-to-end, complete eight-recording experiment with eight
independent folders and eight accepted parts.
