# Open Ephys Agent Native 0.0.2

This is the narrow, stdio-only MCP bridge for the official Open Ephys GUI
v1.0.2 baseline. It pins agent contract 0.0.2 and MCP protocol 2024-11-05.

It publishes exactly twelve explicit tools for capabilities, status, recording
options, recording filename components, recording directory, CPU, disk, and elapsed time.
Every call verifies the exact pinned capability document before accessing Open
Ephys. Only the MCP bridge outbound client is loopback-only and refuses
redirects. The raw Open Ephys API listener still binds `0.0.0.0`, remains
network-exposed, and can bypass MCP RECORD approval. Run it only on a trusted
network or firewall. There is no listener or C++ change outside the scoped
recording-directory capability metadata in this slice.

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

This bundle is limited to offline contract and CI verification:
hardware_verified:false; scientific_verified:false. It makes no hardware or
scientific validation claim.

Run the stdlib tests with:

```powershell
py -3 -m unittest Tests.AgentNative.test_open_ephys_mcp_v002 Tests.AgentNative.test_open_ephys_release_bundle
```
