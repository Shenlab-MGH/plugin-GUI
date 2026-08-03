# Open Ephys v1.1.0 agent-native core r0.1.0

This directory contains the smallest Windows MCP surface for the Open Ephys
v1.1.0 agent-native prerelease. It exposes existing Open Ephys operations only:
capability discovery, status, recording filename text, and CPU usage.

## Pins

- Open Ephys baseline: `v1.1.0`
- agent contract and bundle: `r0.1.0`
- API capability contract: `0.1.1`
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

## Safety and verification

Every tool first compares `GET /api/capabilities` with the exact checked-in
fixture. Mutations perform pre-read, mutation, and post-read. Entering RECORD
requires the explicit `approve_recording=true` argument. HTTP 400/409,
unexpected schemas, and failed postconditions become MCP tool errors.

The upstream recording endpoint is not transactional. For r0.1.0 the filename
tool therefore accepts exactly one component per call and rejects Windows path,
traversal, control-character, reserved-name, and invalid-character forms before
HTTP. This keeps the required filename SET capability without claiming atomic
multi-component mutation.

Run the dependency-independent tests with:

```powershell
python -m unittest Tests.AgentNative.test_open_ephys_mcp_r010 -v
```

Official `mcp==2.0.0` is not installed on this instrument PC, and installation
is outside the agent safety scope. Therefore the official `Client(mode="auto")`
interop gate is not part of the green core CI job. The prerelease must remain
Draft until a locked official SDK artifact and hash are provisioned for a
separate ephemeral Windows interop gate.
