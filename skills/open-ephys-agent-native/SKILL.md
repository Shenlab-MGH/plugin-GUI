---
name: open-ephys-agent-native
description: Safely inspect and control the Open Ephys v1.1.0 core through the r0.1.0 MCP tools. Use for capability discovery, acquisition or recording mode changes, recording filename changes, and CPU/status reads on the Windows Open Ephys agent-native build.
---

# Open Ephys Agent Native

Use only the MCP tools declared here. This skill is pinned to:

- contract: r0.1.0
- Open Ephys baseline: 1.1.0
- MCP protocol: legacy 2024-11-05

## Workflow

1. Call `oe_get_capabilities` before every operation sequence. Stop if it reports a contract mismatch.
2. Read current state before proposing a mutation:
   - Call `oe_get_status` for IDLE, ACQUIRE, or RECORD.
   - Call `oe_get_recording_filename` for prepend, base, and append text.
   - Call `oe_get_cpu` for CPU usage.
3. Use `oe_set_status` only for an explicit requested target mode. Pass `approve_recording=true` only when the user explicitly authorized entering RECORD; never infer approval from setup context.
4. Use `oe_set_recording_filename` with only the fields the user requested to change.
5. Treat every tool error, HTTP 400/409, schema mismatch, capability mismatch, or postcondition failure as a stop condition. Report it; do not retry a mutation or fall back to GUI clicks.
6. Report the verified before and after values returned by mutation tools.

## Tool surface

- `oe_get_capabilities`: verify the exact API contract before other work.
- `oe_get_status`: read the current mode.
- `oe_set_status`: set IDLE, ACQUIRE, or RECORD with readback.
- `oe_get_recording_filename`: read the three filename components.
- `oe_set_recording_filename`: update one or more filename components with readback.
- `oe_get_cpu`: read CPU usage from 0.0 to 1.0.

Do not assume tools, controls, or workflows outside this list exist in r0.1.0.
