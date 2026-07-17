# Isolated real GUI and endpoint read-only test

Date: 2026-07-17

Scope: real Windows `open-ephys.exe` process and GUI, isolated empty state,
read-only Agent endpoint. No hardware configuration, acquisition, recording,
native port 37497, user plugin, or Agent mutation was permitted.

## Executable and isolation

- Executable:
  `D:\cong\.worktrees\06-open-ephys-agent-v0.0.1\Build-runtime\Release\open-ephys.exe`
- SHA-256:
  `7372F47050B2478BD8BE1B5210B300243C1A437C7E79963594FC9D6AB0695823`
- Test state roots:
  - `D:\cong\runtime-state\oe-real-gui-test-20260717`
  - `D:\cong\runtime-state\oe-auth-status-test-20260717`
- Test ports: 38501 and 38502.
- Required arguments verified from the live process:
  `--state-dir`, `--no-http`, `--no-user-plugins`,
  `--agent-uia-readonly`, and the dedicated `--agent-port`.

The first live-process verifier passed every check: correct executable, state
directory argument, native HTTP locked off, user plugins locked off, read-only
UIA, one loopback listener on 127.0.0.1, and no PID-owned listener on 37497.

## Real UIA and capture evidence

Computer Use discovered the real windows `Load a Default Configuration` and
`Open Ephys GUI`. The accessibility tree exposed:

- root group `oe.agent.root`;
- Acquisition button `oe.transport.acquisition`;
- Recording button `oe.transport.recording`;
- semantic default-configuration radio buttons for Acquisition Board, File
  Reader, and Neuropixels-PXI;
- semantic Load and window-close controls.

Windows Graphics Capture returned the real 1200 x 800 Open Ephys window. It
showed GUI v1.0.2, an empty signal chain, zero acquisition/recording time, and
the default-configuration dialog. The screenshot was inspected in the active
Computer Use session but was not persisted into the repository, so this file
does not claim a durable screenshot artifact.

Two attempts to close the initial modal through its indexed UIA close element
were refused before input because the helper reported stale/changed window
state. No coordinate fallback was used. This is a real fail-closed targeting
result. Later cleanup used a refreshed main-window identity and `Alt+F4` only
after the authenticated native status had reported IDLE.

## Real endpoint evidence

With the first process running:

- `GET http://127.0.0.1:38501/v1/status` without authorization returned 401;
- `/api/status` returned 404;
- only 127.0.0.1:38501 listened;
- port 37497 did not listen.

The second process used a fresh 48-byte random token that remained only in the
launching process environment. An authenticated read-only `/v1/status` call
returned:

```json
{
  "schema_version": "oe-agent-control-preview/v0.0.1",
  "backend": "in-process-agent-endpoint",
  "mode": "IDLE",
  "revision": 1
}
```

The token was not written to a file or printed. No mutation endpoint was
called.

## Cleanup verification

Both Open Ephys test processes were closed after the IDLE/read-only checks.
Final PowerShell and Computer Use discovery found:

- zero running `open-ephys` processes;
- zero listeners on 37497, 38501, or 38502;
- Open Ephys app state `isRunning=false`, with no windows.

## Explicit qualification boundary

This is real process/UI/endpoint testing, not Source Sim acquisition or an
eight-part experiment. No validated Source Sim signal-chain XML was available.
The packaged OneBox XML contains simulated metadata, but `--no-user-plugins`
prevents loading its required external plugin, and the available runtime
package predates the current source. Therefore this run does not claim:

- Source Sim configuration loading;
- acquisition or Binary Record Node output;
- recording start/stop safety;
- preset selection or readback;
- native recording-directory creation;
- structural/scientific QC;
- complete eight-part experimental capability.

