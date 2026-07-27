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
- MCP server: `agent_native/open_ephys_mcp_server.py`
- UIA inspection helper: `Resources/Scripts/inspect_open_ephys_uia.ps1`
