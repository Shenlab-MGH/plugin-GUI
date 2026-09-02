---
name: open-ephys-agent-native
description: Safely inspect and control Open Ephys v1.1.0 through the 0.0.2 MCP tools. Use for capability discovery, acquisition or recording mode changes, recording options, filename or parent-directory changes, and CPU/disk/time status reads on the Windows Open Ephys agent-native build.
---

# Open Ephys Agent Native

Use only the MCP tools declared here. This skill is pinned to:

- contract: 0.0.2
- Open Ephys baseline: 1.1.0
- MCP protocol: legacy 2024-11-05
- hardware_verified:false
- scientific_verified:false

## Workflow

1. Call `oe_get_capabilities` before every operation sequence. Stop if it reports a contract mismatch.
2. Read current state before proposing a mutation:
   - Call `oe_get_status` for IDLE, ACQUIRE, or RECORD.
   - Call `oe_get_recording_options` for expanded / new_directory_requested / force_new_directory.
   - Call `oe_get_recording_filename` for prepend, base, and append text.
   - Call `oe_get_recording_directory` for the recording parent directory.
   - Call `oe_get_cpu` for CPU usage.
   - Call `oe_get_disk` for disk usage.
   - Call `oe_get_time` for elapsed acquisition/recording time.
3. Use `oe_set_status` only for an explicit requested target mode. Pass `approve_recording=true` only when the user explicitly authorized entering RECORD; never infer approval from setup context.
4. Use `oe_set_recording_options` for exactly one boolean option per call (`expanded`, `force_new_directory`, or `new_directory_requested`).
5. Use `oe_set_recording_filename` for exactly one component per call. Reject paths, traversal, control characters, Windows-invalid characters, reserved device names, and trailing spaces or dots. Multi-component writes are intentionally unavailable because the recording filename HTTP endpoint is not atomic.
6. Use `oe_set_recording_directory` only while the observed mode is not RECORD and only with a non-empty drive-letter-rooted Windows path. UNC, device, and extended namespace paths are rejected before HTTP. The tool normalizes the submitted path, does not preflight directory existence, and requires an equivalent PUT response and GET readback. Treat a 200 response that did not apply the path as a failed postcondition.
7. Treat every tool error, HTTP 400/409, schema mismatch, capability mismatch, uncertain mutation outcome, or postcondition failure as a stop condition. Report it; do not retry a mutation or fall back to GUI clicks.
8. Report the requested, submitted, before, and after values returned by the directory mutation tool.

## Tool surface

- `oe_get_capabilities`: verify the exact API contract before other work.
- `oe_get_status`: read the current mode.
- `oe_set_status`: set IDLE, ACQUIRE, or RECORD with readback.
- `oe_get_recording_options`: read recording options.
- `oe_set_recording_options`: set exactly one recording option with readback.
- `oe_get_recording_filename`: read the three filename components.
- `oe_set_recording_filename`: update exactly one validated filename component with readback.
- `oe_get_recording_directory`: read the recording parent directory.
- `oe_set_recording_directory`: normalize and update one drive-letter-rooted Windows parent directory with readback.
- `oe_get_cpu`: read CPU usage from 0.0 to 1.0.
- `oe_get_disk`: read disk usage from 0.0 to 1.0.
- `oe_get_time`: read elapsed time display and related status fields.

Do not assume tools, controls, or workflows outside this list exist in 0.0.2.
