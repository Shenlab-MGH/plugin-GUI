# Agent Transport Control Contract v0.0.1

Status: experimental, not approved for research acquisition.

## Purpose

Agent-facing transport control uses explicit target states instead of retrying
GUI toggle commands. Each request contains:

- a non-empty audit request ID;
- a target state: `IDLE`, `ACQUIRE`, or `RECORD`;
- the exact observed state revision on which the decision was based.

The planner rejects requests when the current state is unknown, the revision is
stale, the request ID is empty, or the target is invalid. Asking for the state
that is already active succeeds without executing an action, even if the
request carries the pre-transition revision. This makes a lost-response retry
safe: a stale request can never mutate state, while an already-achieved target
can be acknowledged idempotently.

`AgentTransportCoordinator` is the required execution boundary for future
Agent/API adapters. It reads state once to plan, reads it again immediately
before committing the first action, and verifies every step's post-state and
revision before continuing. A failed action, changed precondition, unknown
state, or mismatched readback terminates the sequence. In particular,
`RECORD → IDLE` cannot proceed to stop acquisition unless stopping recording
was independently observed as `ACQUIRE`.

The current ControlPanel runtime reads acquisition from
`AudioComponent::callbacksAreActive()` and recording from every Record Node's
`getRecordingStatus()`. A partial Record Node state or recording without active
callbacks is reported as `UNKNOWN`, not coerced to a successful GUI state.
Agent recording also clears the legacy `forceRecording` flag and performs
Record Node, directory, and synchronization checks immediately before entering
the existing high-level recording path. Unlike the ordinary GUI, the Agent
path never enters the unsynchronized Yes/No override handler and fails closed.

`ControlPanel::applyAgentTransportRequest()` is message-thread-only and rejects
re-entrant transactions. Future HTTP, MCP, named-pipe, or UIA adapters must post
work to the JUCE message thread; they must not call the method directly from a
worker thread.

`RECORD` currently proves active callbacks and unanimous Record Node
`getRecordingStatus()` values. It does not yet prove sustained file growth,
first-block arrival, or long-term writer-thread health. Those are separate
experiment-quality checks required before claiming reliable data capture.

## Transition plan

| Current | Target | Ordered actions |
|---|---|---|
| IDLE | ACQUIRE | start acquisition |
| IDLE | RECORD | request atomic safe recording start |
| ACQUIRE | IDLE | stop acquisition |
| ACQUIRE | RECORD | request safe recording start |
| RECORD | ACQUIRE | stop recording |
| RECORD | IDLE | stop recording, then stop acquisition |

“Request safe recording start” must enter the existing ControlPanel validation
path. From IDLE it must reuse the complete `startAcquisition(true)` behavior,
not call the lower-level `startRecording()` primitive. A future executor must
not bypass Record Node presence, directory validity, synchronization checks,
audio/graph startup, or callbacks. The planner produces a proposal, never an
execution authorization. Every step carries its expected state before and
after execution; the executor must stop immediately on mismatch or failed
readback.

## Accessibility boundary

The v0.0.1 semantic allowlist contains exactly:

- `oe.transport.acquisition`
- `oe.transport.recording`

The main Open Ephys accessibility tree remains disabled. The registry is only
the policy and metadata foundation; it is not proof that Windows UI Automation
can currently discover or invoke these controls.

Each descriptor is schema-versioned and declares its semantic control kind,
required UIA pattern, and whether recording preflight is mandatory. It does not
store toggle-derived target states. A provider must read the authoritative
transport state immediately before forming an explicit target-state request.

A Windows provider is acceptable only when it:

1. exposes exactly the allowlisted transport controls;
2. maps the stable IDs to native UIA `AutomationId`;
3. identifies UIA-originated requests separately from ordinary UI events;
4. converts activation to a revision-checked target-state request;
5. uses the existing recording safety checks;
6. performs independent state readback;
7. passes observe-only and controlled Source Sim runtime verification.

Signal-chain editing, plugin parameters, recording paths, plugin installation,
quit, and other controls are outside the v0.0.1 accessibility allowlist.
