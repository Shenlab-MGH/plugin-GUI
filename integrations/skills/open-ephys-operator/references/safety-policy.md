# Safety policy

## Authority

The Open Ephys fork owns state and authorization. The MCP server translates
only typed read operations. The Skill is instruction, not a permission
boundary. A scientist's visible GUI and experiment procedures remain primary.

## Required stop conditions

Stop tool use and report `BLOCKED` when any of these occurs:

- `ok` is false or a tool fails;
- schema, product, MCP, or GUI version mismatches;
- `session_id` is missing or changes during one observation;
- revision moves unexpectedly between the two status reads;
- `online` is false, phase is not `READY`, or mode is `UNKNOWN`;
- permission is not `OBSERVE` or mutation is advertised;
- preflight `pass` is false;
- the native endpoint cannot authenticate or respond.

## Prohibited behavior

- Never connect to legacy port 37497.
- Never send transport requests or call unlisted tools.
- Never use shell commands, generic HTTP, UIA Invoke/Toggle, coordinates,
  image matching, or plugin messages to control Open Ephys.
- Never stop, restart, close, or reconfigure Open Ephys after a client error.
- Never claim recording success from mode, UIA value, video, file presence, or
  elapsed time alone.
- Never store or print `OE_AGENT_TOKEN`.

## Evidence language

Use “observed” for a typed value returned by the native fork. Use “unverified”
for acquisition quality, synchronization, file growth, metadata, hashes,
probe identity, shank selection, or recording integrity in this read-only
slice.
