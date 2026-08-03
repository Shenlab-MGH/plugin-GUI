---
name: open-ephys-agent-native
description: Safely operate the narrow Open Ephys v1.0.2 Core R0 MCP surface.
---

# Open Ephys Agent Native Core R0

This Skill is pinned to Open Ephys GUI `1.0.2`, contract: `0.0.4`, API
capability contract `0.0.4`, and legacy MCP protocol `2024-11-05`.

Use only these tools: `oe_get_capabilities`, `oe_get_status`, `oe_set_status`,
`oe_get_recording_options`, `oe_set_recording_options`,
`oe_get_recording_filename`, `oe_set_recording_filename`,
`oe_get_recording_directory`, `oe_set_recording_directory`, `oe_get_config`, `oe_get_processors`, `oe_get_cpu`,
`oe_get_disk`, and `oe_get_time`.

Read the relevant state before a mutation and inspect its returned readback.
Every tool independently verifies the exact twelve Core 0.0.4 capabilities. Only
the MCP bridge outbound client is loopback-only and rejects redirects. The raw
Open Ephys API listener binds `0.0.0.0`, remains network-exposed, and can bypass
MCP RECORD approval. Use a trusted network or firewall. The raw API listener
remains unchanged. This slice also makes the existing `GET /api/processors`
read bounded and message-thread safe; it adds no route or processor/configuration mutation.

To request `RECORD`, include `approve_recording:true` in that same
`oe_set_status` call. This approval is local safety metadata and is never sent
to Open Ephys. A status conflict may report
`record_nodes_not_synchronized`; resolve synchronization in Open Ephys before
trying again.

Set exactly one boolean field with `oe_set_recording_options`, and exactly one
valid Windows filename component with `oe_set_recording_filename`. These
mutations use pre-read, write, and post-read verification and fail closed on a
mismatch.

Use `oe_get_config` to read the current signal-chain snapshot. Accept it only as
the returned official `{info}` SETTINGS XML. It is not a saved configuration,
does not expose save/load/write, and has no native UIA automation ID in this
slice.

Use `oe_get_processors` for a read-only loaded-processor inventory. It validates
the existing raw snapshot and returns only `id/name/predecessor`. Node IDs are
session/configuration-scoped and may be recycled after clear or load. `name` is
the current mutable display name returned by the official interface at snapshot
time. `predecessor` is the official single current source-path field, or `null`,
and is not full topology. The MCP result exposes no parameters or streams and
provides no add/delete/configure action. On Windows, the matching UIA root and
items are read-only; item IDs use `oe.processor.<node_id>` and their titles track
the editor display name without changing that ID. This is new semantic exposure,
not a claim that official v1.0.2 already exposed processor UIA.
`WindowsUIAutomation_tests` performs real external Windows UI Automation
observation from a worker COM client, including root/item IDs, roles, current
title, and absence of Invoke. This local gate passes; hosted CI remains pending
until the exact branch head completes the configured Windows workflow. CTest
enforces a 30-second CTest hard process timeout around the external COM smoke.

Use `oe_get_recording_directory` to read the parent directory. Use
`oe_set_recording_directory` only for a non-empty absolute Windows path. It
retains the original request, normalizes and reports the submitted Windows path,
sends only `parent_directory`, and refuses while status is `RECORD`. The bridge
does not preflight existence or create a path. Official Open Ephys alone checks
`File.exists()`; its raw API may return 200 without applying the path, so accept
success only after an equivalent PUT response and GET readback. A returned
value does not establish writability, free space, applicability to existing
Record Nodes, or use by a successful recording.

This bundle is offline-contract-and-local-Windows-UIA verified only:
`hardware_verified:false` and `scientific_verified:false`.
