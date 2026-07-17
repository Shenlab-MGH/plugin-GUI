# Agent Behavior Audit and Realistic Simulation Qualification

**Status:** binding design and release gate for v0.0.1 and the later complete
eight-part experiment release

**Priority:** scientific accuracy, safety, traceability, then speed and
convenience

## 1. Required outcome

The system must prove both of these independently:

1. Open Ephys produced the intended eight scientifically reviewable recording
   units in eight unique native top-level directories.
2. Every externally observable Agent, human, MCP, API, UIA, GUI-input, recorder,
   watchdog, and filesystem action that influenced the run is attributable,
   ordered, correlated, tamper-evident, and reviewable.

Neither video alone nor a final data folder proves how a run occurred. A run
passes only when the scientific artifact record and the Agent behavior ledger
agree.

"All Agent behavior" means all observable inputs, outputs, decisions, intents,
tool calls, side effects, and verifications. It does not mean hidden model
chain-of-thought, which is neither available nor an appropriate scientific
record. The ledger stores a bounded decision record: selected action, reason
code, considered safe alternatives, cited observations, expected postcondition,
and policy result.

## 2. Evidence hierarchy

Evidence is ranked as follows:

1. native Open Ephys state and typed endpoint readback;
2. native recording files, counters, metadata, and verified hashes;
3. structured MCP/API/UIA/input events with stable semantic targets;
4. timestamped window screenshots and video segments;
5. pixel inference or operator narrative.

Lower-ranked evidence can explain higher-ranked evidence but cannot override
it. Video is required for review where configured, but missing video never
causes semantic replay to guess or perform a mutation.

## 3. Unified action lifecycle

Every mutation uses one correlation ID and the following durable sequence:

```text
observation
  -> decision
  -> approval (when required)
  -> intent committed to the audit ledger
  -> execution started
  -> result: succeeded | failed | unknown
  -> independent authoritative readback
  -> artifact binding
  -> workflow state transition
```

An action without committed intent is unauthorized. An action without terminal
result and independent readback is unresolved. It cannot be displayed as
successful or compiled into a replay workflow. A timeout after a non-idempotent
mutation becomes `unknown` and requires reconciliation; it is never blindly
retried.

## 4. Common event envelope

The initial Python core uses schema `oe-agent-action-audit/v0.0.1`. The durable
collector must extend it without losing these fields:

- schema version, event ID, session ID, experiment run ID, sequence;
- UTC timestamp and producer monotonic timestamp/epoch;
- actor and producer identity;
- event kind, target kind, stable semantic target identity;
- correlation ID, causation ID, workflow step, part, and attempt;
- expected and observed state/revision;
- sanitized payload and typed outcome;
- artifact references, each with MIME type, bytes, and SHA-256;
- previous event hash and event hash.

Event families cover session lifecycle, observation, decision, tool intent and
result, native request and readback, UIA snapshot and action, GUI input, human
approval/takeover/resume, recorder/video segment, filesystem artifact,
verification, clock discontinuity, recovery, and policy/error events.

The same `run_id`, `correlation_id`, command ID, approval ID, and native request
ID connect the Agent conversation to MCP, the native fork, UIA evidence, video,
and the resulting recording unit.

## 5. Durable collector and artifact store

The in-memory builder is only a contract core. The operational v0.0.1 design is:

- one local Audit Collector assigns the authoritative global sequence;
- SQLite WAL is the authoritative append-only event store;
- each mutation intent is committed before the side effect;
- content-addressed evidence storage holds screenshots, video segments, UIA
  snapshots, bounded responses, logs, and manifests;
- an artifact is flushed to a temporary file, atomically renamed to its digest,
  then referenced by a committed event;
- JSONL is an export format, not the recovery authority;
- no audit or video I/O runs on an Open Ephys acquisition/audio thread;
- audit failure blocks new mutation but cannot block an authorized emergency
  stop;
- Open Ephys raw neural samples remain in their native recording store.

The collector seals checkpoints at least every 100 events or 10 seconds. Each
checkpoint covers the chain head and artifact Merkle root and uses an
HMAC/signature key protected by Windows DPAPI/CNG. A plain SHA-256 chain detects
editing but does not by itself prevent complete-chain replacement, so a release
cannot claim tamper resistance until checkpoint protection is implemented and
tested.

## 6. Screen, window, UIA, and input recording

Capture is target-aware rather than full-desktop by default:

- bind capture to the Open Ephys process and HWND, with process start time and
  executable hash to prevent PID/HWND reuse;
- record monitor topology, logical/physical bounds, DPI, resolution, window
  visibility, occlusion, and foreground ownership;
- segment video and hash every segment; record PTS start/end, frame count,
  dropped frames, codec, and capture target;
- correlate every Agent/human GUI action with the relevant before/after frame
  range and UIA snapshot;
- record UIA AutomationId, role/control type, pattern, ancestor selector,
  candidate count, enabled/visible state, and authoritative postcondition;
- record physical coordinates only as diagnostic evidence, never as the replay
  authority;
- capture multi-screen or a selected window according to a signed capture
  policy; an unexpected target switch creates an evidence gap and pause.

For typed text, clipboard, terminal, other applications, and window titles,
privacy is deny-by-default. Sensitive text is omitted or represented by length
and a session-scoped HMAC. Raw full-desktop recording requires explicit human
consent and restricted encrypted storage.

## 7. Human takeover

Human control is authoritative:

- `HUMAN_TAKEOVER` must be committed before Agent mutation authority is revoked;
- from takeover through a valid resume, Agent mutation count must be zero;
- Agent may continue bounded read-only observation if capture consent allows;
- resume requires a fresh native/UIA snapshot and a new scoped approval bound to
  the current session, run, state revision, and action digest;
- unknown external mouse, keyboard, UIA, or native state changes are treated as
  human/external takeover;
- after an unclean restart, an unfinished takeover remains human-owned;
- a stale Agent request is cancelled, never automatically replayed.

## 8. Replay and workflow learning

The audit ledger is evidence, not executable automation. A workflow compiler
accepts only fully verified correlations and emits semantic steps containing:

- precondition and exact approved state revision;
- typed MCP/native action or stable UIA selector;
- expected postcondition and independent verification method;
- timeout, idempotency class, failure state, and human takeover rule;
- references to the source observations and artifacts.

v0.0.1 permits deterministic `dry-run` replay and later assisted step-by-step
replay. Unattended live replay is forbidden. Secrets and approval tokens are
never replayed. Window IDs, PIDs, and coordinates are re-resolved, not copied.

## 9. Privacy and secret boundary

Serialization uses per-event allowlists and redacts before canonical encoding
and hashing. The following never enter ordinary logs or exports:

- bearer/API/OAuth tokens, passwords, cookies, signing or encryption keys;
- complete environment variables, raw authorization headers, or unfiltered
  request bodies/query strings;
- clipboard contents, arbitrary keystrokes, or terminal contents;
- direct subject/operator identity and identifying absolute paths;
- raw neural data or unrestricted UI tree/OCR dumps.

Evidence that cannot yet be visually redacted is classified sensitive,
encrypted, access-controlled, and excluded from ordinary replay. Every release
runs seeded canary scans across the database, JSONL, artifacts, stdout/stderr,
manifests, and crash metadata. Permitted plaintext secret leakage is zero.

## 10. Exact realistic simulation gates

All random seeds, generator version, fault schedule, executable/config/plugin
hashes, OS/build, schema hash, and first failure are retained. A failed shard
cannot be made green by retry; the defect requires a root cause and regression
test.

### 10.1 Pull-request gate

- 2,000 model seeds x 500 events = 1,000,000 state/audit events;
- 200 crash-cut cases;
- 500 semantic GUI replay cases;
- all 400 versioned golden traces once the corpus exists;
- all deterministic unit, source-contract, protocol, and privacy tests.

### 10.2 Nightly gate

- 10,000 model seeds x 2,000 events = 20,000,000 events;
- 5,000 crash cases;
- 5,000 GUI/UI-tree perturbation cases;
- 8-hour synthetic audit/replay soak;
- accelerated full eight-part simulations with the real fork where isolation
  and a pinned Source Sim configuration are available.

### 10.3 Release gate

- 50,000 model seeds x 2,000 events = 100,000,000 events;
- 400 golden traces: 100 legal, 100 rejected, 50 takeover, 50 recovery,
  50 video-boundary, and 50 historical-schema traces;
- 20,000 crash cases across serialization, write, flush/fsync, acknowledgement,
  mutation, readback, and result-commit cut points;
- 10,000 mixed loss/duplicate/reorder/truncation/corruption traces, in addition
  to deterministic matrices;
- 3,600 deterministic and 5,000 randomized wall/monotonic clock fault traces;
- 4,720 clean/fault/video-boundary capture scenarios;
- 10,000 display/DPI/topology GUI cases and 10,000 randomized UI-tree
  perturbations;
- 50,000 secret-canary placements;
- 100 clean complete accelerated eight-part runs and 1,000 faulted complete-run
  attempts, producing 8,800 planned part attempts;
- 30 complete real-process accelerated Source Sim cycles;
- 10 complete real-process nominal-duration Source Sim cycles with the same
  2-3 minute per-part parameters used by the intended experiment;
- 24-hour journal/replay soak and 4-hour actual target-window capture soak;
- 100 consecutive collector/replayer restart cycles;
- three consecutive separately authorized supervised real-device eight-part
  qualification runs before research-data capability can be approved.

Simulation that uses only mocks cannot satisfy the real-process gates. A
real-process Source Sim run launches the packaged fork in an isolated state
directory, uses the real Record Node/Binary engine and Windows message thread,
writes actual files to an isolated qualification root, loads them using the
pinned official Python tools, and verifies process shutdown and manifest hashes.
It never shares a desktop/device session with an active experiment.

## 11. Fault matrix

The qualification harness injects at least these faults at every applicable
workflow state boundary:

- stale revision, wrong session/run/approval/action digest, duplicate command,
  lost/delayed response, endpoint/MCP/collector/dashboard exit;
- process crash, message-thread stall, restart/reconciliation, unknown state;
- directory collision, case collision, unwritable/missing root, disk reserve,
  disk full, writer growth stop, extra/missing recording unit;
- wrong/unknown preset, config/plugin/executable hash change, sync false/unknown,
  late first block, modal dialog, device disconnect;
- manual pause/takeover at intent, native call, readback, and result boundaries;
- log short write, torn tail, fsync failure, bit flip, replacement, deletion,
  duplication, reordering, and clock discontinuity;
- video gap/overlap, missing/duplicate segment, PTS reset, encoder stall, wrong
  window, minimize/occlusion, DPI/monitor change, lock, crash, and disk full;
- missing, ambiguous, disabled, stale, localized, or reordered UIA targets and
  focus theft.

Safety-critical fault classes run at least 100 injections per cut point. No
accepted earlier part may be altered by a later failure.

## 12. Non-negotiable pass criteria

- intact-log replay final state equals the independent reference model: 100%;
- event/artifact modification, deletion, insertion, reordering, or truncation
  detection: 100%;
- duplicate scientific mutation: 0;
- unresolved or incomplete evidence incorrectly reported complete: 0;
- sequence loss/duplication in acknowledged committed events: 0;
- mutation during human takeover: 0;
- timeout followed by blind retry of a non-idempotent mutation: 0;
- wrong-window or coordinate-fallback mutation: 0;
- plaintext secret canary leakage: 0;
- clean semantic GUI replay and clean eight-part simulation success: 100%;
- exactly eight unique unsuffixed native directories for every successful run;
- every accepted part has correlated preset, state, duration, native unit,
  structural QC, scientific review, Agent behavior, and artifact evidence;
- video gaps are always explicit and never change the semantic replay result;
- three supervised real-device qualification runs complete 8/8 consecutively
  before real-experiment approval.

Zero failures in 1,000 independent full-run simulations gives a simple
one-sided 95% upper confidence bound of roughly 0.3% for an unobserved failure
rate; it does not prove zero real-world risk. Real-device qualification and
human scientific review remain mandatory.

## 13. Current status and implementation order

Already present:

- fsync JSONL in the legacy gateway;
- request/audit IDs in transport control;
- immutable experiment events and deterministic reducer;
- initial in-memory `ActionRecord` hash-chain builder and verifier with recursive
  secret redaction, takeover enforcement, and mutation intent/readback checks.
- a tested single-process SQLite WAL prototype that commits canonical events,
  resumes the hash chain after restart, rejects detected record tampering,
  reports unresolved mutation correlations, and stores verified
  content-addressed artifacts. It is not yet the integrated authoritative
  multi-producer collector and has no protected checkpoint signature.

Not yet complete:

1. integration and hardening of the SQLite WAL collector, multi-producer
   sequence allocation, crash-cut recovery, and access control;
2. DPAPI/CNG-protected checkpoints, artifact CAS, sealing, and offline verifier;
3. integration of every MCP/native/UIA/human/video/filesystem producer;
4. crash reconciliation and deterministic reference replay engine;
5. window-selective video capture and event-to-frame correlation;
6. semantic GUI recorder/compiler/replayer and takeover UI;
7. executable PR/nightly/release qualification harnesses and evidence reports;
8. Source Sim, soak, supervised device, and human usability evidence.

Implementation order is durable collector -> reference replay oracle ->
producer integrations -> UIA/GUI recorder -> video correlation -> large-scale
qualification. Video does not precede the semantic ledger because it cannot
provide a reliable replay oracle by itself.
