# Parameter Route Segment Parity Plan

**Goal:** Make every Open Ephys 1.0.2 parameter name returned by the API usable unchanged through the HTTP API and OE-only MCP/skill, including names containing spaces, without weakening single-path-segment safety.

**Base:** `agent-native-v102-parameter-parity-contract` at `3045a94441530ed17d45ba82d56822f49bcfe6f2` (official Open Ephys `1.0.2`, agent contract `0.1.1`).

## Global Constraints

- This is one narrow agent-native parity package. Do not change acquisition, recording, processor, parameter, stream, or Neuropixels behavior.
- Preserve all existing API routes, payload shapes, supported legacy parameter names, UIA AutomationIds, and MCP command names.
- The GUI/API identity remains the API-returned parameter `key` and `uia.automation_id`; the MCP and skill must discover rather than synthesize them.
- A parameter name is exactly one URL path segment. Accept a valid percent-encoded space (`%20`) and other names that cpp-httplib safely decodes into one non-empty segment; reject decoded `/`, `\`, `.` and `..`, malformed percent escapes, empty segments, and control characters.
- Processor and stream parameter GET and PUT routes must use the same admission rule.
- Strict TDD: add a real behavioral test, observe the expected failure against production code, then implement the minimum fix. Do not use source-text assertions.
- Keep the OE baseline at `1.0.2`. Synchronize the agent surface manifest, versioned fixture, MCP serverInfo, MCP validation, OE-only skill, and tests at agent contract `0.1.2` only after runtime behavior is proven.
- Windows is the release target. Use a serial build lease, `CL_MPCount=1`, and `/m:1`; do not run concurrent C++ builds.
- Follow existing Open Ephys code style and contribution conventions. Keep commits reviewable and push only after focused and regression verification.

## Task 1: Safe processor and stream parameter route segments

Files likely involved:

- `Source/Utils/OpenEphysHttpServer.h`
- the existing API test target and its CMake registration

1. Identify the existing real HTTP-server test seam and bundled cpp-httplib decoding behavior.
2. RED: add behavioral tests that send actual HTTP requests for processor and stream parameter GET and PUT routes. Prove that `gain%20mode` reaches the handler as `gain mode` and that a known parameter can be read and written through that route.
3. RED: cover legacy names such as `gain-mode_1.0` to prevent regression.
4. RED: prove rejection of decoded slash, backslash, empty, `.`, `..`, malformed percent escapes, and control-character names before parameter lookup or mutation.
5. GREEN: introduce one small shared admission rule used by all four routes and make the minimum route-registration change needed for valid single-segment names.
6. Verify error status and body follow the server's existing not-found/bad-request conventions; do not invent a parallel error schema.

## Task 2: Synchronize API contract, MCP, and OE-only skill at 0.1.2

Files:

- `agent_native/open_ephys_agent_surface.json`
- `agent_native/open_ephys_agent_contract_v1_0_2.json`
- `agent_native/open_ephys_mcp_server.py`
- `Tests/AgentNative/test_open_ephys_mcp_server.py`
- `skills/open-ephys-agent-native/SKILL.md`
- `agent_native/README.md` only if its user workflow needs clarification

1. RED: add behavior tests requiring agent contract `0.1.2` across manifest, fixture, MCP `initialize.serverInfo.version`, and skill.
2. RED: prove MCP renders processor and stream parameter names containing spaces as one percent-encoded path segment and refuses values that would introduce a slash, backslash, dot-segment, control character, or empty segment.
3. GREEN: add the smallest shared MCP path-segment validator/renderer and apply it only to dynamic parameter-name fields.
4. GREEN: describe the runtime guarantee in the versioned contract and surface manifest so mixed `0.1.1`/`0.1.2` assets fail closed.
5. GREEN: update the OE-only skill workflow to discover the parameter first, use its returned key, and send it as one percent-encoded segment. It must not claim real-device validation.

## Verification and Publication

- Focused C++ RED/GREEN route tests with captured failure and passing evidence.
- Existing API suite.
- Existing parameter/custom UI accessibility tests (no UIA behavior regression).
- `python -m unittest discover -s Tests/AgentNative -p 'test_*.py' -v`.
- `python -m py_compile agent_native/open_ephys_mcp_server.py`.
- Parse both JSON files and workflow YAML.
- `git diff --check` and clean worktree after commits.
- Independent task review after each task and a final whole-branch review.
- Push branch `agent-native-v102-parameter-route-segments`, open a small Draft PR stacked on `agent-native-v102-parameter-parity-contract`, and wait for required CI checks.
