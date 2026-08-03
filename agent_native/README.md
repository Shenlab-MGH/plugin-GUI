# Open Ephys Agent Native Core R0

This is the narrow, stdio-only MCP bridge for the official Open Ephys GUI
v1.0.2 baseline. It pins agent contract r0.1.3 and MCP protocol 2024-11-05.

It publishes exactly fourteen explicit Core R0.1.3 tools for capabilities, status,
recording options, recording filename components, recording parent directory,
the current signal-chain configuration snapshot, loaded processor inventory, CPU,
disk, and elapsed time.
Every call verifies the exact pinned capability document before accessing Open
Ephys. Only the MCP bridge outbound client is loopback-only and refuses
redirects. The raw Open Ephys API listener still binds `0.0.0.0`, remains
network-exposed, and can bypass MCP RECORD approval. Run it only on a trusted
network or firewall. The raw API listener remains unchanged. This slice makes
the existing `GET /api/processors` read bounded and message-thread safe, as
already done for `GET /api/config`; it does not add a route or any processor,
configuration, save, load, or write behavior.

Status, options, filename, and directory mutations use pre-read, write, and post-read
verification. RECORD requires same-call `approve_recording:true`, which is
never forwarded to Open Ephys. Filename mutations validate one Windows-safe
component before the write. Directory mutation requires a non-empty absolute
Windows path, normalizes it with Windows path semantics, sends only
`parent_directory`, and refuses while status is `RECORD`. The bridge does not
preflight existence, create directories, or infer success from HTTP 200. The
official Open Ephys handler alone checks `File.exists()` and its raw API may
return 200 without applying the path; the MCP tool therefore requires an
equivalent PUT response and GET readback.

`oe_get_config` preserves the official `{ "info": "..." }` response and returns
it only when `info` is a string containing well-formed XML with an exact
`SETTINGS` root. This is a snapshot read, not a saved file or a UIA locator. The
capability contract reports an empty UIA automation ID transparently because no
equivalent native UI Automation element exists in this slice.

`oe_get_processors` validates the exact existing raw processor snapshot, then
returns only a closed `id/name/predecessor` projection. Each node ID is
session/configuration-scoped and may be recycled after clear or load. `name` is
the current mutable display name returned by the official interface at snapshot
time. `predecessor` is the node ID from the official single current source-path
field, or `null`; it is not full topology. The MCP projection does not expose
parameters or streams. The EditorViewport is a read-only UIA list and each
existing GenericEditor is a read-only list item named `oe.processor.<node_id>`;
its title follows the current editor display name while its description keeps
the constructor-time name distinct. This is new semantic exposure for Windows
UI Automation, not a claim that official v1.0.2 already supplied processor UIA.
`WindowsUIAutomation_tests` performs real external Windows UI Automation
observation from a worker COM client: it finds the root and dynamic item by
AutomationId, checks List/ListItem roles and title, and verifies no Invoke
pattern. This local gate passes; hosted CI remains pending until the branch is
submitted and its exact-head workflow completes. CTest enforces a 30-second
CTest hard process timeout around this external COM smoke test.

This bundle is limited to offline contract and local external Windows UIA verification:
hardware_verified:false; scientific_verified:false. It makes no hardware or
scientific validation claim.

Run the stdlib tests with:

```powershell
py -3 -m unittest Tests.AgentNative.test_open_ephys_mcp_r013 Tests.AgentNative.test_open_ephys_release_bundle
```
