# Tool workflows

## Standard observation

1. `oe_get_identity`: bind product, native schema, GUI version, session, and
   revision.
2. `oe_get_capabilities`: require the exact four read-only tools and four
   resources; reject mutation capability.
3. `oe_get_runtime_status`: record online, phase, mode, session, revision, and
   provenance.
4. `oe_run_readonly_preflight`: require every named check and overall `pass`.
5. `oe_get_runtime_status`: verify the same session and explain any revision
   change.

No tool accepts input fields. Treat an unknown-field acceptance as a protocol
fault and stop.

## Resource use

- `open-ephys://identity`: compact identity snapshot.
- `open-ephys://capabilities`: exact allowlist and permission.
- `open-ephys://status`: compact native status.
- `open-ephys://sop/8-shank`: planning boundary, not an executable workflow.

Resources are convenience views. For a report, call the tools so failures are
explicit and current.

## Interpretation

- `IDLE`: acquisition and recording are observed off.
- `ACQUIRE`: callbacks are observed active and recording is observed off.
- `RECORD`: recording is observed active; this does not prove file integrity.
- `UNKNOWN`: contradictory or unavailable state; stop.

Never convert a state label into an implied command or experimental result.
