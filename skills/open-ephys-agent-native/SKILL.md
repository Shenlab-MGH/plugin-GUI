# Open Ephys Agent-Native Control

Use this skill when controlling the Shenlab `plugin-GUI` fork as an agent.

## Control priority

1. Prefer the local HTTP API through `agent_native/open_ephys_mcp_server.py`.
2. Use Windows UI Automation for GUI-only controls, menus, dialogs, popups, and
   safety prompts. The canonical capability id is the UIA AutomationId.
3. Use screenshot-based desktop control only as a last-resort diagnostic.

## Required operating loop

1. Discover capabilities with `oe_list_commands` or `GET /api/capabilities`.
2. Read current state before a mutation.
3. Execute one named command with `oe_post_command`.
4. Read back state from the API or UIA and report the evidence.
5. Stop on mismatched readback, unavailable controls, HTTP errors, or any GUI
   safety prompt that requires human approval.

The MCP bridge accepts only loopback HTTP(S) endpoints. Composite recording
settings report their individual directory, filename, and engine capability IDs;
they are not the `oe.control.recording` start/stop toggle. A `RECORD` command
also requires an explicit same-run `confirm_recording: true` argument.

The OE agent surface is versioned as a contract. For the current Windows
baseline, use the `open-ephys-agent` contract `0.1.1` against Open Ephys GUI
`1.0.2` (commit `c91afebcfb0678a667fb93f6312ed33c56ec640f`). Keep these files
together and reject a mixed-version setup:

- `agent_native/open_ephys_agent_contract_v1_0_2.json` — core release metadata,
  required UIA IDs, and a small API↔MCP route parity matrix;
- `agent_native/open_ephys_agent_surface.json` — the complete MCP/API command
  manifest and UIA locator surface.

The manifest is the source of truth for the full API route list. The contract
fixture intentionally checks only the core parity slice; API-only commands and
GUI-only UIA controls remain explicit in their respective surfaces and must not
be assumed to have a one-to-one mapping.

## Parameter UIA workflow

Before locating a parameter in the GUI, call `get_processor_parameters`,
`get_stream_parameters`, `get_parameter`, or `get_stream_parameter` first.
Each parameter response includes `key`, `display_name`, `description`,
`enabled`, `deactivate_during_acquisition`, and `uia.automation_id` alongside
the legacy `name`, `type`, and string `value` fields. Pass that returned
`uia.automation_id` to `oe_uia_locator` as `automation_id`.

Parameter AutomationIds follow `oe.parameter.<sanitised parameter key>`, but
do not construct or guess them from a parameter name or key. The API response
is the authority. Generic route capabilities such as `oe.processor.parameter`
are not parameter UIA AutomationIds and cannot be passed to `oe_uia_locator`.

## Safety boundaries

- Treat `RECORD` as a high-risk action. Do not enter recording without explicit
  human approval for the current run and a same-run readback plan.
- Prefer UIA/GUI handling when Open Ephys displays a safety prompt.
- Do not bypass disabled GUI controls through API calls. If a capability is not
  available, report the blocker.
- Do not infer scientific validity from API success. API success means command
  delivery and mechanical readback only.

## Files

- Manifest: `agent_native/open_ephys_agent_surface.json`
- Versioned contract fixture: `agent_native/open_ephys_agent_contract_v1_0_2.json`
- MCP server: `agent_native/open_ephys_mcp_server.py`
- UIA inspection helper: `Resources/Scripts/inspect_open_ephys_uia.ps1`
