# Parameter UIA/API/MCP/Skill Parity Plan

**Goal:** Expose each existing Open Ephys 1.0.2 processor and stream parameter through one canonical identity shared by the GUI, Windows UIA, HTTP API, MCP, and the OE-only skill.

**Scope:** Contract-only improvement. Do not change parameter values, routing, acquisition behavior, processor behavior, or Neuropixels behavior. Preserve the existing API fields and routes. Parameter names containing spaces and stream selector identity are separate follow-up PRs.

## Task 1: Canonical parameter identity and API response

Files likely involved:

- `Source/Processors/Parameter/Parameter.h/.cpp`
- `Source/Processors/Parameter/ParameterEditor.cpp`
- `Source/Utils/OpenEphysHttpServer.h`
- an existing appropriate C++ test target

1. RED: add behavioral C++ tests proving that a processor-owned parameter and a stream-owned parameter serialize with the legacy `name`, `type`, and string `value` fields plus:
   - `key`
   - `display_name`
   - `description`
   - `enabled`
   - `deactivate_during_acquisition`
   - `uia.automation_id`
2. RED: prove the API AutomationId exactly matches the ID used by `ParameterEditor`, including sanitization of the real `Parameter::getKey()`.
3. GREEN: introduce the smallest transport-neutral canonical parameter AutomationId helper and call it from both the UI metadata adapter and HTTP serializer.
4. Keep all existing routes and legacy response fields compatible.

## Task 2: Versioned agent contract and MCP behavior

Files:

- `agent_native/open_ephys_agent_surface.json`
- `agent_native/open_ephys_agent_contract_v1_0_2.json`
- `agent_native/open_ephys_mcp_server.py`
- `Tests/AgentNative/test_open_ephys_mcp_server.py`
- `skills/open-ephys-agent-native/SKILL.md`

1. RED: add Python behavior tests requiring contract version `0.1.1`, the canonical dynamic parameter rule `oe.parameter.<sanitised parameter key>`, and the documented API response fields.
2. RED: require `oe_uia_locator` to accept a parameter AutomationId returned by the API, while rejecting generic dynamic capability names such as `oe.processor.parameter` and arbitrary strings.
3. GREEN: update the fixture, manifest validation, MCP tool schema/behavior, and skill workflow together.
4. The skill must instruct agents to call a parameter-list/get endpoint first, then use the returned `uia.automation_id`; it must not synthesize dynamic IDs.

## Verification and publication

Run fresh evidence before commit:

- focused C++ RED/GREEN test(s)
- existing UI parameter accessibility tests
- `python -m unittest discover -s Tests/AgentNative -p 'test_*.py' -v`
- `python -m py_compile agent_native/open_ephys_mcp_server.py`
- parse both JSON files and the workflow YAML
- `git diff --check`

Then perform an independent diff review, make one focused commit, push the branch, open a Draft PR based on `agent-native-v102-contract-parity-fixture`, and wait for both Unit and Integration CI checks.
