# Open Ephys v1.1.0 agent-native 0.0.4

This directory contains the smallest Windows MCP surface for the Open Ephys
v1.1.0 agent-native prerelease. It exposes existing Open Ephys operations only:
capability discovery, status, recording options, filename and parent-directory
control, a read-only signal-chain configuration snapshot, read-only processor
inventory, plus CPU, disk, and elapsed-time reads.

## Pins

- Open Ephys baseline: `v1.1.0`
- agent contract and bundle: `0.0.4`
- API capability contract: `0.0.4`
- MCP protocol: legacy `2024-11-05`
- modern MCP: not supported

The server deliberately returns JSON-RPC `-32601 Method not found` for
`server/discover`. A dual-era client can then fall back to legacy initialize.
Do not add partial modern discovery: advertising modern mode would require the
complete modern result and request contract.

## Run

Start Open Ephys, then run:

```powershell
python agent_native/open_ephys_mcp_server.py
```

The default Open Ephys API is `http://127.0.0.1:37497`. A test or isolated
local instance can override it with `--base-url`, but non-loopback URLs are
rejected. MCP JSON is written to stdout; diagnostics are written to stderr.

Point Codex, Claude Code, or another stdio MCP client at the command above and
use the paired skill at `skills/open-ephys-agent-native/SKILL.md`.

## Tool surface

- `oe_get_capabilities`: verify the exact API contract before other work.
- `oe_get_status`: read the current mode.
- `oe_set_status`: set IDLE, ACQUIRE, or RECORD with readback.
- `oe_get_recording_options`: read recording options.
- `oe_set_recording_options`: set exactly one recording option with readback.
- `oe_get_recording_filename`: read the three filename components.
- `oe_set_recording_filename`: update exactly one validated filename component with readback.
- `oe_get_recording_directory`: read the recording parent directory.
- `oe_set_recording_directory`: normalize and update one absolute Windows parent directory with readback.
- `oe_get_config`: read and validate the current signal-chain SETTINGS XML.
- `oe_get_processors`: read loaded processors as id, current display name, and current-path predecessor.
- `oe_get_cpu`: read CPU usage from 0.0 to 1.0.
- `oe_get_disk`: read disk usage from 0.0 to 1.0.
- `oe_get_time`: read elapsed time display and related status fields.

## Safety and verification

Every tool first compares `GET /api/capabilities` with the exact checked-in
fixture. Mutations perform pre-read, mutation, and post-read. Entering RECORD
requires the explicit `approve_recording=true` argument. HTTP 400/409,
unexpected schemas, and failed postconditions become MCP tool errors.

The upstream recording endpoint is not transactional. The filename
tool therefore accepts exactly one component per call and rejects Windows path,
traversal, control-character, reserved-name, and invalid-character forms before
HTTP. This keeps the required filename SET capability without claiming atomic
multi-component mutation.

The 0.0.4 directory setter accepts only a non-empty drive-letter-rooted Windows
path and normalizes it before PUT. UNC, device, and extended namespace paths
are rejected before HTTP. It refuses to mutate while the observed mode is
RECORD, does not preflight existence, and requires a Windows-path-equivalent PUT
response and GET readback. If Open Ephys returns 200 without applying the path,
the tool reports a failed postcondition. A transport failure or non-authoritative
write response reports an uncertain mutation outcome and must not be retried.

The read-only processor inventory is session/configuration scoped. It returns
id, current display name, and current-path predecessor in full graph order. It
is not full topology and the MCP projection intentionally omits parameters or
streams after validating the complete API response. This adds no new Open Ephys
functionality and provides no add, delete, load, save, probe, shank, or timer
controls. `hardware_verified=false` and `scientific_verified=false` remain
explicit until the corresponding gates are run.

Run the dependency-independent tests with:

```powershell
python -m unittest Tests.AgentNative.test_open_ephys_mcp_v0_0_4 -v
```

Official `mcp==2.0.0` remains absent from the instrument host. The separate
`official-mcp-v2-interop.yml` Windows gate downloads the complete binary-only
CPython 3.12 wheelhouse under `RUNNER_TEMP`, verifies every pinned SHA-256 hash,
installs only into an ephemeral venv, and exercises
`Client(stdio_client(...), mode="auto")` against this server. The wheel lock and
download manifest are checked in under `agent_native/interop`; no SDK package,
wheel, cache, or credential is committed or installed on the instrument host.
Local verification does not promote the release-bundle claim: it remains
pending until this exact commit succeeds in the remote Windows gate.
