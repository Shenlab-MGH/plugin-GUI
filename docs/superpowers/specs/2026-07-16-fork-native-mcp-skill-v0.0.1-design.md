# Open Ephys Fork Native MCP and Skill v0.0.1 Design

**Status:** Proposed for operator review

**Product version:** `0.0.1`

**Official baseline:** Open Ephys GUI tag `v1.0.2`, commit
`c91afebcfb0678a667fb93f6312ed33c56ec640f`

## 1. Objective

Ship the Open Ephys fork, a local standard MCP server, and a portable Agent
Skill as one versioned product. The system must let compatible agents observe
and perform the lab's common Open Ephys workflow through typed semantic
operations while preserving the native scientist-facing GUI.

Safety and recording integrity take precedence over automation coverage. The
system must fail closed when identity, version, state freshness, configuration,
probe, plugin, approval, audit, or readback cannot be proved.

## 2. Product Boundary

The product contains three independently testable components:

1. **Open Ephys fork:** owns authoritative state, transport execution,
   experiment safety checks, UIA semantics, action provenance, and final
   authorization decisions.
2. **Local MCP server:** translates standard MCP tools and resources into the
   fork's typed native protocol. It owns no acquisition state and cannot grant
   itself permission.
3. **Portable Skill:** teaches an agent the approved SOP, tool ordering,
   failure rules, human-takeover procedure, and reporting format. It stores no
   credentials and is not a permission boundary.

The fork must continue recording if the MCP server or agent exits. The MCP
server must remain restartable without restarting Open Ephys. The Skill must
not call the legacy native HTTP API, generic UI automation, or a shell to
control Open Ephys.

## 3. Compatibility and Version Handshake

Every component reports all of the following:

- product version `0.0.1`;
- fork source commit;
- Open Ephys GUI version;
- Plugin API version;
- native protocol schema `oe-agent-native/v0.0.1`;
- MCP package version;
- Skill version;
- process-scoped `session_id`;
- monotonic transport `revision`;
- observation timestamp and freshness.

The MCP server refuses mutation when the native protocol major version is not
exactly compatible. The Skill instructs agents to stop when any component
version is missing or mismatched. Read-only diagnostic output may remain
available during a mismatch.

The MCP server targets the stable MCP protocol revision `2025-11-25` for
v0.0.1. The breaking `2026-07-28` revision is still a release candidate on the
design date and is not a production target. MCP protocol handling must be
isolated behind an adapter so a later product release can add the new revision
without changing fork safety logic.

## 4. Native Fork Architecture

### 4.1 Authoritative state

The fork derives acquisition state from
`AudioComponent::callbacksAreActive()` and recording state from every Record
Node. It must report `UNKNOWN` when callbacks and Record Nodes disagree or
Record Nodes disagree with each other.

Each coherent state change increments `revision`. Every mutation request must
contain the exact observed `session_id` and `revision`. A stale request cannot
execute. An already-achieved target may return an idempotent success without a
second action.

### 4.2 Typed native operations

The fork exposes typed operations for the approved scope. It must not expose a
generic HTTP request, arbitrary CoreServices call, arbitrary plugin message,
raw UIA action, or shell command.

Mutation requests contain:

```json
{
  "request_id": "agent-generated-id",
  "expected_session_id": "process-session-id",
  "expected_revision": 12,
  "approval_id": "short-lived-approval-id",
  "reason": "operator-visible purpose"
}
```

The fork performs the final state, capability, approval, preflight, and audit
checks immediately before execution on the JUCE message thread. A successful
dispatch is not success; success requires authoritative post-state readback.

### 4.3 Permission levels

The native permission levels are:

| Level | Permitted scope |
|---|---|
| `OBSERVE` | Status, configuration, health, faults, and evidence reads |
| `PREPARE` | Create a session and prepare a new non-colliding recording target |
| `OPERATE_SIM` | Source Sim acquisition and recording after explicit arming |
| `OPERATE_HARDWARE` | Qualified real-hardware operations under a short-lived operator approval |

`ADMIN` is absent from v0.0.1. The default is `OBSERVE`. A Skill, MCP client,
MCP server restart, environment variable, or UIA client cannot upgrade the
permission level. `OPERATE_HARDWARE` remains unavailable until the exact
hardware and plugin profile has passed the separate qualification suite.

### 4.4 Human takeover

The native layer owns a single mutation lease. Manual GUI actions always win:

- a manual state change increments `revision` and invalidates pending stale
  Agent requests;
- pausing automation blocks new mutations but does not automatically stop an
  active recording;
- an MCP disconnect does not alter transport state;
- resuming automation requires a fresh full preflight and approval;
- the GUI remains usable when the Agent endpoint, MCP server, or Skill is
  unavailable.

### 4.5 Semantic action recording

Common GUI and Agent operations pass through the same command boundary and
append an event with:

- origin: `USER_GUI`, `MCP_AGENT`, or future `UIA_AGENT`;
- request and approval identifiers when applicable;
- before and after session/revision/state;
- configuration, probe, shank, and recording target identity;
- execution result and readback evidence;
- monotonic and wall-clock timestamps.

The event log contains no bearer token and no raw neural data. Audit writes are
flushed before a mutation is accepted. An audit failure blocks subsequent
mutations but does not stop an active recording.

## 5. Windows UI Automation v0.0.1

### 5.1 Explicit read-only mode

UIA Agent semantics are disabled by default and enabled only with
`--agent-uia-readonly`. The ordinary visual GUI remains unchanged.

The fork adds a dedicated `AgentAccessibilityRoot` as a narrow accessibility
branch. The existing dynamic editor and plugin tree remains inaccessible to
Agent UIA. The branch contains exactly:

- `oe.agent.root`;
- `oe.transport.acquisition`;
- `oe.transport.recording`.

The two transport nodes read from the authoritative snapshot cache. They
provide a read-only value and no accessibility `press` or `toggle` action.
Windows therefore exposes neither InvokePattern nor TogglePattern.

### 5.2 Stable AutomationId repair

The bundled JUCE Windows provider currently derives AutomationId from element
and ancestor titles instead of `ComponentID`. The fork changes the Windows
mapping to use a non-empty `ComponentID` as the exact AutomationId, with the
existing title-derived behavior retained only as fallback.

This framework change requires a focused JUCE regression test plus a real
Windows UIA integration test. It must not change Name, HelpText, ControlType,
or ordinary components with an empty ComponentID.

### 5.3 UIA acceptance gates

The Windows acceptance test must prove:

- UIA is absent without `--agent-uia-readonly`;
- the Agent root and its two allowlisted transport children are discoverable
  when enabled;
- the Agent root has exactly those two children and no non-allowlisted Open
  Ephys control;
- transport values match the native endpoint snapshot;
- `UNKNOWN` is preserved;
- InvokePattern and TogglePattern are unavailable;
- 10,000 state reads cause zero transport or revision change;
- client termination and malformed UIA requests do not affect Open Ephys.

## 6. Standard MCP Server v0.0.1

### 6.1 Process and transport

The MCP server is a separate local process using standard MCP over `stdio`.
The host launches it as a child process. Standard output contains MCP JSON-RPC
messages only; diagnostics go to standard error. The server connects only to
the fork's authenticated loopback native endpoint.

The implementation stack is Python `3.12`, the official Tier-1 MCP Python SDK
v1.x constrained to `mcp>=1.27,<2`, Pydantic models for strict schemas, and
pytest. Development and reproducible builds use uv with a committed exact
lockfile. The unstable MCP Python SDK v2 is outside v0.0.1. The Windows release
contains a self-contained `open-ephys-agent-mcp.exe`; operators and MCP hosts
do not need a global Python installation.

The initial client acceptance matrix contains:

- Codex on Windows;
- Claude Code on Windows;
- the official MCP Inspector;
- a protocol-level test client independent of either vendor.

Vendor configuration files are adapters, not protocol dependencies.

### 6.2 Read resources

The server exposes compact, versioned resources:

- `open-ephys://identity`;
- `open-ephys://capabilities`;
- `open-ephys://status`;
- `open-ephys://session/manifest`;
- `open-ephys://sop/8-shank`.

Large logs and manifests use pagination or bounded query parameters. Raw
recording data is never returned through MCP.

### 6.3 Tools

The v0.0.1 tool allowlist is grouped below.

**Identity and observation**

- `oe_get_identity`
- `oe_get_capabilities`
- `oe_get_runtime_status`
- `oe_get_safety_status`
- `oe_get_recent_faults`
- `oe_run_readonly_preflight`

**Configuration and hardware observation**

- `oe_get_signal_chain`
- `oe_get_processors`
- `oe_get_streams`
- `oe_get_record_nodes`
- `oe_get_probe_status`
- `oe_get_shank_status`
- `oe_get_recording_configuration`

**Session preparation**

- `oe_create_session`
- `oe_set_subject`
- `oe_prepare_recording_directory`
- `oe_set_recording_name`
- `oe_validate_recording_target`
- `oe_get_session_manifest`

**Transport**

- `oe_start_acquisition`
- `oe_stop_acquisition`
- `oe_start_recording`
- `oe_stop_recording`
- `oe_get_transport_request`

**Shank**

- `oe_select_shank`
- `oe_verify_shank_selection`
- `oe_get_shank_map`

**Recording evidence**

- `oe_get_recording_health`
- `oe_verify_file_growth`
- `oe_verify_recording_artifact`
- `oe_get_recording_summary`

**Workflow and takeover**

- `oe_create_shank_workflow`
- `oe_get_workflow_status`
- `oe_advance_workflow`
- `oe_pause_workflow`
- `oe_resume_workflow`
- `oe_abort_workflow`
- `oe_pause_automation`
- `oe_release_control`
- `oe_get_control_owner`

Every tool declares strict input and output JSON Schemas. Unknown input fields,
oversized strings, invalid identifiers, invalid paths, and out-of-range shank
indices are rejected. Tool outputs always include native `session_id`,
`revision`, observation timestamp, freshness, provenance, and machine-readable
error codes.

### 6.4 Operations intentionally absent

v0.0.1 exposes no generic shell, generic HTTP request, arbitrary UIA invoke,
arbitrary processor parameter setter, plugin installer, signal-chain mutation,
file deletion, data overwrite, application quit, or force-record override.

### 6.5 Neuropixels adapter boundary

Shank mutation is advertised only when the detected Neuropixels plugin exactly
matches a qualified plugin version and exposes typed selection plus independent
semantic readback. Probe serial, selected shank, stream count, channel count,
and sample rate must match the approved plan after selection.

Unsupported or unqualified versions return a capability absence or one of:

- `UNSUPPORTED_PLUGIN_VERSION`;
- `SEMANTIC_READBACK_UNAVAILABLE`;
- `PROBE_IDENTITY_MISMATCH`;
- `SHANK_SELECTION_MISMATCH`.

The system never falls back to coordinates, image matching, or an untyped
plugin configuration string and then reports success.

## 7. Session and Recording Integrity

### 7.1 Recording target rules

Session preparation must:

- canonicalize and confine paths to the approved recording root;
- reject path traversal and reparse-point escapes;
- create new directories without overwriting existing data;
- verify write access and available disk space;
- freeze subject, session, shank, configuration, and probe identity before
  recording;
- never rename an active recording;
- perform any post-close rename atomically within one volume;
- retain both requested and normalized names in the manifest.

### 7.2 Recording success evidence

`RECORD` is necessary but insufficient. A successful shank recording requires:

- active callbacks;
- unanimous Record Node recording state;
- first-block evidence;
- sustained file growth without an over-threshold stall;
- no writer fault;
- adequate remaining disk space;
- requested minimum duration;
- stable file size after stop;
- parseable structure and metadata;
- expected streams, channels, and sample rates;
- final file list, sizes, and SHA-256 values.

Any missing evidence produces an incomplete or failed result, never a success.

## 8. Eight-Shank Workflow

The MCP server exposes an asynchronous, resumable workflow rather than a tool
call that blocks for two to three minutes.

The workflow states are:

```text
CREATED
PREFLIGHT
READY_FOR_SHANK
SHANK_SELECTED
ACQUIRING
RECORDING
VERIFYING
SHANK_COMPLETE
SESSION_COMPLETE
PAUSED
FAILED
ABORTED
```

The plan contains exactly eight unique shank indices unless an approved profile
defines a different count. Each shank has a duration range, with the lab's
default set to 120-180 seconds. The workflow never advances merely because a
timer elapsed; it advances only after recording evidence passes.

Each shank result records probe, shank, configuration, target, actual duration,
health samples, warnings, approvals, state transitions, and artifact hashes.
Restart recovery reconstructs state from the durable manifest and native
readback; it never repeats a mutation whose outcome is unknown.

Pausing or aborting a workflow prevents future workflow mutations but does not
implicitly stop an active recording or acquisition. Transport changes require
their own typed, approved request. This prevents a client disconnect or an
over-broad abort action from damaging an in-progress recording.

## 9. Portable Skill v0.0.1

The canonical skill lives at:

```text
integrations/skills/open-ephys-operator/
├── SKILL.md
├── agents/openai.yaml
├── references/
│   ├── safety-policy.md
│   ├── tool-workflows.md
│   ├── eight-shank-sop.md
│   ├── failure-recovery.md
│   └── reporting-schema.md
└── scripts/
    └── validate-session-manifest.py
```

`SKILL.md` stays below 500 lines and contains only essential routing and
procedural rules. Detailed SOPs and schemas are loaded from one-level-deep
references when required. The manifest validator is deterministic and tested.

The Skill instructs any compatible agent to:

1. verify component identity and compatibility;
2. start in observation mode;
3. run preflight before preparation or operation;
4. stop on `UNKNOWN`, stale evidence, mismatch, or missing capability;
5. obtain and preserve operator approval for mutation;
6. use only typed MCP tools in the documented order;
7. perform independent recording-artifact verification;
8. pause and report rather than improvise around a blocked safety gate;
9. produce the standard scientist-facing completion or incident report.

The Skill never embeds credentials, elevates permissions, calls port 37497,
uses a generic shell to control Open Ephys, or substitutes video/pixels for
semantic readback.

Agents that support the common `SKILL.md` package can install it directly.
Agents without Skill discovery can consume the same SOP references, but the
MCP server remains the interoperability and enforcement layer.

## 10. Packaging and Installation

One release manifest binds:

- fork executable and bundled plugin hashes;
- launcher and verifier hashes;
- native protocol schema;
- MCP server executable/package and dependency lock hashes;
- Skill tree hashes;
- compatible MCP protocol revision;
- compatible Open Ephys, Plugin API, and qualified plugin versions.

The Windows installer/configurator must be reversible and must not alter the
official Open Ephys installation. It creates explicit Codex and Claude Code
MCP configuration snippets, but the canonical server remains vendor-neutral
stdio MCP.

Tokens are generated per launch, passed only through the child environment or
an equivalent process-local channel, redacted from logs, and absent from
manifests and Skill files.

## 11. Testing and Quantitative Release Gates

### 11.1 Automated gates

- all existing fork tests pass;
- new native unit and integration tests pass;
- all MCP schemas pass positive, negative, boundary, and unknown-field tests;
- MCP conformance tests pass for `2025-11-25`;
- MCP Inspector can list and invoke every permitted tool against a fake fork;
- Codex and Claude Code can connect on native Windows;
- Skill structural validation passes;
- the manifest validator passes valid fixtures and rejects corrupted fixtures;
- no secret appears in stdout, stderr, audit, manifest, crash log, or Skill;
- all builds use warnings-as-errors for new isolated code where supported.

### 11.2 UIA gates

- exactly one Agent root with exactly two allowlisted transport children;
- zero InvokePattern and zero TogglePattern providers in read-only mode;
- 10,000 reads produce zero mutation and zero revision change;
- stable AutomationIds across ten clean launches;
- malformed or terminated UIA clients do not crash or stall the GUI.

### 11.3 Source Sim gates

- 100 complete create/prepare/acquire/record/stop/verify cycles;
- zero blind retries;
- zero state-transition false positives;
- zero overwritten files;
- zero unaccounted artifacts;
- every successful cycle has first-block, growth, post-stop stability,
  metadata, and hash evidence;
- a continuous soak at least as long as the longest planned experiment;
- injected MCP, endpoint, UI, and audit failures leave the fork in a defined
  state and preserve active recording unless an explicitly approved stop is
  executed.

### 11.4 Official-versus-fork comparison

Run the same Source Sim configuration sequentially in the pristine official
build and the fork with separate state and output roots. Compare configuration,
streams, channels, sample rates, Record Node metadata, duration, output
structure, parseability, and operational faults. Binary or data-file hashes are
not required to be equal when timestamps or identifiers are expected to vary;
all semantic differences must be explained and approved.

### 11.5 Hardware gate

No Source Sim result authorizes real hardware. `OPERATE_HARDWARE` requires a
separate signed qualification profile for the exact device, probe, firmware,
driver, Open Ephys fork, and plugin hashes. A scientist performs the final
review before experiment use.

## 12. Failure Policy

The system fails closed for mutation on:

- unknown or stale state;
- session or revision mismatch;
- missing or expired approval;
- audit failure;
- native endpoint authentication failure;
- unqualified plugin, probe, device, driver, or configuration;
- invalid or colliding recording target;
- unsynchronized Record Nodes;
- missing first-block or file-growth evidence;
- disk-critical state;
- inconsistent manual takeover state;
- MCP/native protocol incompatibility.

Failure does not automatically terminate the application or an active
recording. The system records the fault, blocks new mutations, exposes the
evidence, and requests human direction.

## 13. v0.0.1 Non-Goals

- arbitrary signal-chain construction;
- arbitrary plugin parameter mutation;
- plugin installation;
- coordinate or image-based control as an operational fallback;
- unsupervised real-hardware experiments;
- force-recording through synchronization warnings;
- deleting or overwriting scientific data;
- automatic application quit;
- streaming raw electrophysiology through MCP;
- treating OBS video as authoritative state;
- modifying the official Open Ephys installation.

## 14. Release Definition

v0.0.1 is releasable when the fork, MCP server, and Skill are built from one
clean commit, bound by one verified manifest, pass all automated and Windows
UIA gates, and complete the Source Sim acceptance suite. Real-hardware
mutation remains disabled until its separate qualification profile passes.
