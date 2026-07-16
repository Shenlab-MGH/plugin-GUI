# Agent Transport Control Contract v0.0.1

Status: experimental, not approved for research acquisition.

## Purpose

Agent-facing transport control uses explicit target states instead of retrying
GUI toggle commands. Each request contains:

- a non-empty audit request ID;
- a target state: `IDLE`, `ACQUIRE`, or `RECORD`;
- the exact observed transport-mode revision used for the decision.

The planner rejects requests when the current state is unknown, the revision is
stale, the request ID is empty, or the target is invalid. Asking for the state
that is already active succeeds without executing an action, even if the
request carries the pre-transition revision. This makes a lost-response retry
safe: a stale request can never mutate state, while an already-achieved target
can be acknowledged idempotently.

## Threading and adapter boundary

External HTTP, MCP, named-pipe, or UIA worker threads must retain a
`shared_ptr<AgentTransportEndpoint>` obtained while the ControlPanel is alive.
They must never retain or call a raw `ControlPanel*`.

The endpoint provides:

- `serviceSnapshot()` for `DETACHED`, `READY`, or `STOPPED` lifecycle plus
  the coherent last transport observation;
- `submit()` for non-blocking target-state submission;
- `query()` for explicit request lifecycle and result lookup.

The thread-safe mailbox permits one pending or active mutation. A duplicate ID
with the same payload is idempotent; the same ID with different content is
rejected. Completed result bodies retain the most recent 128 requests. The
request ID and payload identity remain as a session-scoped tombstone after a
result body expires, so an old ID cannot execute a second mutation.

Accepted requests use an injected scheduler backed by JUCE
`MessageManager::callAsync`. The callback captures the shared endpoint and only
a weak executor lease. The executor performs the mutation on the JUCE message
thread. Direct synchronous mutation is message-thread-only and rejects
re-entrant transactions.

ControlPanel teardown first detaches the executor, shuts down the endpoint, and
clears the legacy global ControlPanel pointer. A callback delivered after
detach cancels its pending request without calling the destroyed component.
The endpoint remains safe to query through existing shared references.

Request lookup distinguishes `UNKNOWN`, `PENDING`, `ACTIVE`, `COMPLETED`,
`EXPIRED`, and `CANCELLED`. Shutdown rejects new work and cancels pending work;
an already active message-thread transaction may finish and publish its result.
Executor exceptions are converted to a terminal result with
`executionFailed`; they cannot leave the mailbox permanently active.
Cancellation also retains a machine-readable reason:
`dispatchUnavailable`, `executorDetached`, or `shutdown`.

`AgentStateStore` is message-thread-owned. Every authoritative read publishes a
complete snapshot into the endpoint's `AgentStateSnapshotCache`, which worker
threads may read without accessing buttons, graph objects, or Record Nodes.
Ordinary GUI dispatch also performs authoritative readback, so manual takeover
updates the same snapshot observed by agents. Hardware/graph fault shutdown
publishes immediately from `disableCallbacks()`, and the ControlPanel timer
periodically reconciles changes that did not pass through an Agent or button
command.

## Planning and execution

`AgentTransportCoordinator` is the required execution boundary. It reads state
once to plan, reads it again immediately before committing the first action,
and verifies every step's post-state and revision before continuing.

A failed action, changed precondition, unknown state, or mismatched readback
terminates the sequence. `RECORD -> IDLE` cannot stop acquisition unless
stopping recording was independently observed as `ACQUIRE`.

| Current | Target | Ordered actions |
|---|---|---|
| IDLE | ACQUIRE | start acquisition |
| IDLE | RECORD | request atomic safe recording start |
| ACQUIRE | IDLE | stop acquisition |
| ACQUIRE | RECORD | request safe recording start |
| RECORD | ACQUIRE | stop recording |
| RECORD | IDLE | stop recording, then stop acquisition |

The safe recording action performs Record Node, directory, and synchronization
checks immediately before execution. It clears the legacy `forceRecording`
flag, never enters the GUI unsynchronized override dialog, and fails closed.
From IDLE it uses the complete `startAcquisition(true)` path; from ACQUIRE it
uses the ControlPanel high-level `startRecording()` path.

## Authoritative transport readback

The ControlPanel runtime reads acquisition from
`AudioComponent::callbacksAreActive()` and recording from every Record Node's
`getRecordingStatus()`.

A partial Record Node state or recording without active callbacks is reported
as `UNKNOWN`, not coerced into a successful GUI state. The Play and Record
button toggles are treated as presentation and intent, not authoritative
runtime evidence.

`RECORD` currently proves active callbacks and unanimous Record Node recording
flags. It does not yet prove sustained file growth, first-block arrival, or
long-term writer-thread health. Those are separate experiment-quality checks
required before claiming reliable data capture.

## Accessibility boundary

The v0.0.1 semantic allowlist contains exactly:

- `oe.transport.acquisition`
- `oe.transport.recording`

Each descriptor is schema-versioned and declares its semantic control kind,
required UIA pattern, and whether recording preflight is mandatory. Button
metadata is populated from this single registry by strong control kind.

The main Open Ephys accessibility tree remains disabled. The registry is only
the policy and metadata foundation; it is not proof that Windows UI Automation
can currently discover or invoke these controls.

A Windows provider is acceptable only when it:

1. exposes exactly the allowlisted transport controls;
2. maps the stable IDs to native UIA `AutomationId`;
3. exposes controlled Invoke without default TogglePattern bypass;
4. identifies UIA-originated requests separately from ordinary UI events;
5. reads the cached authoritative state and forms an explicit target request;
6. submits through the shared endpoint, mailbox, and coordinator;
7. performs independent runtime readback;
8. passes observe-only and controlled Source Sim verification.

Signal-chain editing, plugin parameters, recording paths, plugin installation,
quit, and other controls are outside the v0.0.1 accessibility allowlist.

## Known v0.0.1 integration gaps

- The legacy Open Ephys HTTP server still calls `CoreServices` and the global
  ControlPanel access path. It is not yet migrated to
  `AgentTransportEndpoint`, and must not be presented as the safe Agent API.
- The fork now contains an authenticated HTTP adapter that consumes the shared
  endpoint directly. It binds only to `127.0.0.1:37498`, remains disabled
  unless `OE_AGENT_TOKEN` contains at least 32 characters, accepts explicit
  target-state request documents, and exposes request-result lookup. The
  MainWindow integration deliberately starts it in observe-only mode, so POST
  returns `403 MUTATION_NOT_ARMED`. Mutation is exercised only against a fake
  executor in the isolated C++ HTTP integration test. The adapter is not
  approved for research acquisition because the complete Open Ephys
  application has not been built with MSVC or accepted against Source Sim.
- The in-process adapter currently has no rate limiter, browser dashboard,
  operator approval exchange, or durable audit writer. Treat its bearer token
  as a local development credential and do not expose port 37498 beyond the
  loopback interface.
- Status is an event-driven and periodically reconciled transport snapshot. It
  does not yet include an observation timestamp or prove first-block arrival,
  sustained file growth, disk flush, or writer health; it must not be treated
  as experiment-quality recording evidence.
- No MCP, named-pipe, or production UIA adapter consumes the endpoint yet.
- A stopped endpoint retains the last observed transport mode as evidence, but
  reports `STOPPED` separately. Remote clients must never interpret the retained
  mode as proof that the application is still online.
- Session-scoped idempotency tombstones are intentionally retained for the
  process lifetime. A network adapter must impose authentication, request-rate
  limits, ID-size limits, and a defined session restart policy.
- Full MSVC Release build, CTest, Windows UIA discovery, and Source Sim GUI
  runtime verification remain blocked until Visual Studio Build Tools is
  installed.

## In-process HTTP adapter

Set a fresh per-launch token before starting the custom fork:

```powershell
$alphabet = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789'
$env:OE_AGENT_TOKEN = -join ((1..48) | ForEach-Object {
    $alphabet[(Get-Random -Maximum $alphabet.Length)]
})
```

The server is not started when the token is absent or shorter than 32
characters. Every route requires:

```text
Authorization: Bearer <OE_AGENT_TOKEN>
```

The preview schema is `oe-agent-control-preview/v0.0.1`; it intentionally does
not claim wire compatibility with the Python sidecar's
`oe-agent-gateway/v1`. Every server process generates a new `session_id`.
Mutation documents must echo it as `expected_session_id`, preventing an old
request from a prior Open Ephys process from executing after revision counters
reset.

The v0.0.1 preview routes are:

- `GET /v1/status`
- `POST /v1/transport/requests`
- `GET /v1/transport/requests/{request_id}`

The server is owned by `MainWindow` and is stopped before legacy HTTP, audio,
or processor-graph teardown. It never calls `CoreServices`, `AccessClass`, or a
raw `ControlPanel*`; all reads and mutations go through the shared
`AgentTransportEndpoint`.

The preview request body is:

```json
{
  "request_id": "demo-001",
  "expected_session_id": "<session_id from GET /v1/status>",
  "target_mode": "ACQUIRE",
  "expected_revision": 4
}
```

`request_id` and `expected_session_id` are restricted to 1-128 ASCII
alphanumeric characters plus `.`, `_`, `:`, and `-`. The MainWindow-hosted
server currently rejects this POST regardless of body because v0.0.1 is
observe-only.
