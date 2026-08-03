# Open Ephys Agent Native Core R0

This is the narrow, stdio-only MCP bridge for the official Open Ephys GUI
v1.0.2 baseline. It pins agent contract r0.1.0 and MCP protocol 2024-11-05.

It publishes exactly ten explicit Core R0 tools for capabilities, status,
recording options, recording filename components, CPU, disk, and elapsed time.
Every call verifies the exact pinned capability document before accessing Open
Ephys. The HTTP transport is loopback-only and refuses redirects.

Status, options, and filename mutations use pre-read, write, and post-read
verification. RECORD requires same-call `approve_recording:true`, which is
never forwarded to Open Ephys. Filename mutations validate one Windows-safe
component before the write.

This bundle is limited to offline contract and CI verification:
hardware_verified:false; scientific_verified:false. It makes no hardware or
scientific validation claim.

Run the stdlib tests with:

```powershell
py -3 -m unittest Tests.AgentNative.test_open_ephys_mcp_r010 Tests.AgentNative.test_open_ephys_release_bundle
```
