---
name: open-ephys-operator
description: Safely observe and assess Open Ephys GUI v1.0.2 through the typed open-ephys-agent MCP server. Use for Open Ephys runtime identity, acquisition or recording status, read-only preflight, eight-shank workflow planning, human-takeover assessment, incident reporting, or SOP review. This v0.0.1 skill must not trigger mutations, replace scientist approval, or claim recording success.
---

# Open Ephys Operator

Operate as a read-only scientific observer. Treat the native fork as
authoritative and typed MCP output as evidence. Never infer state from video,
pixels, button appearance, or elapsed time.

## Mandatory safety boundary

Read [safety-policy.md](references/safety-policy.md) before using tools. This
version permits exactly:

- `oe_get_identity`
- `oe_get_capabilities`
- `oe_get_runtime_status`
- `oe_run_readonly_preflight`

Do not use a shell, port 37497, arbitrary HTTP, UIA Invoke/Toggle, coordinates,
or plugin messages to control Open Ephys. Do not invent or request unavailable
mutation tools. Stop and report when any tool returns `ok: false`, mode is
`UNKNOWN`, identity is missing, a version differs, or preflight fails.

## Observation workflow

Read [tool-workflows.md](references/tool-workflows.md), then call tools in this
order:

1. Call `oe_get_identity`.
2. Require product `open-ephys-agent`, product/MCP version `0.0.1`, native
   schema `oe-agent-control-preview/v0.0.1`, and GUI version `1.0.2`.
3. Call `oe_get_capabilities`. Require permission `OBSERVE`, mutation false,
   and the exact four-tool allowlist above.
4. Call `oe_get_runtime_status`. Preserve `session_id`, `revision`, `mode`, and
   provenance.
5. Call `oe_run_readonly_preflight`. Continue only when `ok` and `pass` are
   both true.
6. Call `oe_get_runtime_status` again before reporting. If session or revision
   changed unexpectedly, mark the report `BLOCKED` and explain the mismatch.

Never describe this read-only preflight as approval to acquire or record.

## Eight-shank requests

Read [eight-shank-sop.md](references/eight-shank-sop.md). Produce an observation
or planning report only. State explicitly that selecting shanks, starting
acquisition, starting recording, timing 120-180 seconds, renaming data, and
artifact verification are unavailable in this slice.

## Failures and takeover

Read [failure-recovery.md](references/failure-recovery.md). A disconnect,
timeout, client exit, or human takeover never authorizes a stop command. Do not
restart or close Open Ephys. Preserve the last proven session/revision and ask
the scientist to review the native GUI.

## Reporting

Read [reporting-schema.md](references/reporting-schema.md). Use only
`OBSERVATION_ONLY`, `BLOCKED`, or `INCIDENT`; never use `SUCCESS`. Validate a
JSON report with:

```powershell
python scripts/validate-session-manifest.py report.json
```

Report verified facts separately from unavailable evidence and next human
actions.
