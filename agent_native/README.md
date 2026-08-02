# Agent-native Open Ephys control

This folder is the agent-facing bridge for the Shenlab `plugin-GUI` fork. It
documents and serves the controls that an agent should use before falling back
to visual desktop automation.

## Why this exists

Open Ephys already has a GUI and a local HTTP API. A general computer-use agent
can click screenshots, but that is brittle for lab operation: pixels do not
describe state, safety locks, valid parameters, or readback. This fork exposes
the same operations through:

- stable Windows UI Automation IDs for visible GUI controls;
- a local HTTP capability surface on `http://127.0.0.1:37497`;
- a small MCP server that turns those capabilities into typed tools.

The goal is not to add new acquisition science. The goal is to let an agent
understand and control the existing software through explicit API/UIA contracts.

## What agents should use

1. Use `GET /api/capabilities` or `oe_list_commands` to discover controls.
2. Use `oe_post_command` for named commands. It presents every mutation as a
   command-style action even when the compatibility Open Ephys route is still
   `PUT` or legacy `GET`.
3. Use Windows UIA by AutomationId for menus, dialogs, popups, and GUI safety
   prompts that are not API-safe.
4. Use screenshots only for diagnosis, never as the primary control surface.

For stream discovery, use the typed `oe_list_streams` and `oe_get_stream`
tools. Stream indexes are valid only for the current running configuration, so
discover streams again after a graph or stream-order change. Use only the
API-returned stream AutomationId for UIA lookup.

## Local MCP

Run:

```bash
python3 agent_native/open_ephys_mcp_server.py
```

Useful direct probes:

```bash
python3 agent_native/open_ephys_mcp_server.py --list-commands
python3 agent_native/open_ephys_mcp_server.py --command get_status
python3 agent_native/open_ephys_mcp_server.py --command set_mode --arguments '{"mode":"ACQUIRE"}'
```

The GUI must be running for commands that call the local HTTP API.
