# Open Ephys Agent Gateway v0.0.1

This is the first runnable Agent-facing adapter for the installed Open Ephys
GUI while the custom C++ fork is waiting for MSVC.

It uses only Python's standard library and binds only to `127.0.0.1:37498`.
The dashboard and clients never call the native Open Ephys port directly.

## Start in observe-only mode

```powershell
pwsh -NoProfile -File tools\windows\Start-AgentGateway.ps1
```

The console prints the dashboard URL and a newly generated bearer token. Open
the dashboard and paste the token.

Observe-only is the default. No Open Ephys mutation is possible.

## API

- `GET /v1/status`
- `POST /v1/transport/requests`
- `GET /v1/transport/requests/{request_id}`

All `/v1` routes require:

```text
Authorization: Bearer <startup token>
```

Example request:

```json
{
  "request_id": "demo-001",
  "target_mode": "ACQUIRE",
  "expected_revision": 4
}
```

The Gateway uses target states, not toggles. Every mutation step requires
legacy REST state readback. A lost or timed-out response is never blindly
retried. The custom C++ fork now contains the first authenticated loopback
adapter over `AgentTransportEndpoint`. It still requires a complete MSVC build
and Source Sim acceptance before it can replace this observe-only monitor in
operator workflows.

## Mutation gates

`-ArmSim` is necessary but not sufficient. Mutation is allowed only when:

1. the loaded configuration is detected as simulated;
2. port 37497 is not listening on a wildcard interface;
3. Windows Firewall has no Public inbound allow rule for Open Ephys.

On the current lab computer, the native server listens on `0.0.0.0:37497` and
Open Ephys has Public inbound allow rules. The Gateway therefore reports
`NATIVE_API_EXPOSED` and remains observe-only even with `-ArmSim`.

Inspect the current listener and firewall state without changing anything:

```powershell
pwsh -NoProfile -File tools\windows\Protect-OeNativeApi.ps1
```

From an elevated PowerShell session, explicitly add and verify an inbound block
rule:

```powershell
pwsh -NoProfile -File tools\windows\Protect-OeNativeApi.ps1 -Apply
```

The Gateway still performs its own live check at every startup. The script does
not enable mutation by itself.

The raw neural data remains owned by Open Ephys. The Gateway audit is JSONL and
contains session safety evidence, accepted intent, each attempted transition,
readback, terminal results, authentication failures, and shutdown. Writes are
flushed and synchronized to disk; an audit failure disables subsequent
mutations. Bearer tokens are never stored.

## Intentional observe-only boundary

The official v1.0.2 native API has no atomic compare-and-set operation. A human
or another native client can change state in the small interval between the
Gateway's final GET and PUT. Readback can detect the result afterward but cannot
prevent that race.

For that reason, the current installed-GUI adapter must remain observe-only
while the native server is wildcard-bound. Safe human takeover and mutation
require the custom fork's in-process `AgentTransportEndpoint`, where expected
revision and execution are coordinated on the JUCE message thread.

The current dashboard is therefore a working authenticated monitor and
protocol demo, not an approved research-acquisition controller.
