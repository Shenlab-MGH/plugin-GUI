# Open Ephys MCP and Skill Read-Only v0.0.1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a working vendor-neutral stdio MCP server and portable Skill that expose the fork's authenticated runtime state without permitting mutation.

**Architecture:** A Python 3.12 process uses the official MCP Python SDK v1.x and calls only the fork's allowlisted authenticated loopback status endpoint. Domain code is independent from MCP transport, the MCP adapter owns tool/resource schemas, and the Skill teaches strict observation, preflight, takeover, and reporting rules. This slice intentionally exposes no mutation tool.

**Tech Stack:** Python 3.12, `mcp>=1.27,<2`, Pydantic v2 strict models, pytest, pytest-asyncio, uv exact lockfile, standard MCP over stdio.

## Global Constraints

- Keep stdout reserved for MCP protocol messages; send diagnostics only to stderr.
- Read endpoint URL from `OE_AGENT_BASE_URL` and require loopback HTTP.
- Read bearer token only from `OE_AGENT_TOKEN`; require at least 32 characters and never log it.
- Accept only native schema `oe-agent-control-preview/v0.0.1` and GUI version `1.0.2`.
- Expose only `oe_get_identity`, `oe_get_capabilities`, `oe_get_runtime_status`, and `oe_run_readonly_preflight`.
- Expose only `open-ephys://identity`, `open-ephys://capabilities`, `open-ephys://status`, and `open-ephys://sop/8-shank`.
- Never call port 37497, submit transport requests, invoke UIA, or execute a shell command.
- Treat `UNKNOWN`, stale/missing identity, authentication failure, non-loopback URL, and version mismatch as fail-closed preflight failure.

---

### Task 1: Reproducible MCP package and strict native client

**Files:**
- Create: `integrations/mcp/pyproject.toml`
- Create: `integrations/mcp/src/open_ephys_agent_mcp/__init__.py`
- Create: `integrations/mcp/src/open_ephys_agent_mcp/config.py`
- Create: `integrations/mcp/src/open_ephys_agent_mcp/models.py`
- Create: `integrations/mcp/src/open_ephys_agent_mcp/native_client.py`
- Create: `integrations/mcp/tests/test_config.py`
- Create: `integrations/mcp/tests/test_native_client.py`
- Create: `integrations/mcp/uv.lock`

**Interfaces:**
- Produces: `Settings.from_environment() -> Settings` with loopback-only `base_url` and redacted token handling.
- Produces: `NativeStatus`, a strict Pydantic model with schema, session, phase, GUI version, mode, revision, online, and mutation fields.
- Produces: `NativeClient.get_status() -> NativeStatus` using a five-second bounded request.

- [ ] **Step 1: Write failing configuration and fake-HTTP client tests**

Test valid loopback configuration plus rejection of missing/short tokens, HTTPS/non-loopback/wildcard hosts, port 37497, unknown fields, invalid modes, HTTP errors, invalid JSON, and oversized responses.

- [ ] **Step 2: Run tests and observe import failures**

Run: `uv run pytest tests/test_config.py tests/test_native_client.py -q`

Expected: FAIL because package modules do not exist.

- [ ] **Step 3: Implement the minimal strict settings, models, and client**

Use `urllib.request` so the runtime has no second HTTP dependency. Limit response reads to 65,537 bytes and reject bodies above 65,536 bytes. Convert transport and schema failures to stable `NativeClientError(code, message)` values without including the bearer token.

- [ ] **Step 4: Lock dependencies and run tests**

Run: `uv lock && uv run pytest tests/test_config.py tests/test_native_client.py -q`

Expected: all tests pass and `uv.lock` pins exact artifacts.

- [ ] **Step 5: Commit**

```powershell
git add integrations/mcp
git commit -m "feat: add strict read-only native MCP client"
```

### Task 2: Standard MCP tools, resources, and protocol acceptance

**Files:**
- Create: `integrations/mcp/src/open_ephys_agent_mcp/service.py`
- Create: `integrations/mcp/src/open_ephys_agent_mcp/server.py`
- Create: `integrations/mcp/src/open_ephys_agent_mcp/__main__.py`
- Create: `integrations/mcp/tests/test_service.py`
- Create: `integrations/mcp/tests/test_mcp_protocol.py`

**Interfaces:**
- Produces: `ObservationService.identity/status/capabilities/preflight` returning JSON-safe strict output.
- Produces: `create_server(service) -> FastMCP` with four tools and four resources.
- Produces: `python -m open_ephys_agent_mcp` stdio entrypoint.

- [ ] **Step 1: Write failing service tests**

Assert exact version fields, exact four-tool allowlist, no mutation capability, status passthrough, and preflight failures for offline, non-READY, UNKNOWN, schema mismatch, and GUI mismatch.

- [ ] **Step 2: Run service tests and observe failures**

Run: `uv run pytest tests/test_service.py -q`

Expected: FAIL because the service is absent.

- [ ] **Step 3: Implement observation service and make service tests pass**

Return compact dictionaries containing `product_version`, `native_schema_version`, `mcp_package_version`, `session_id`, `revision`, `mode`, and machine-readable checks. Do not manufacture observation timestamps not supplied by the fork; report `observation_timestamp_available: false`.

- [ ] **Step 4: Write and run failing in-memory MCP protocol tests**

Use the official SDK client/session test transport to initialize, list tools/resources, invoke all four tools, and read all four resources. Assert the exact allowlist and strict empty input schemas for no-argument tools.

- [ ] **Step 5: Implement the FastMCP adapter and stdio entrypoint**

Set server instructions with the safety boundary in the first 512 characters. Keep all startup diagnostics on stderr and allow the MCP SDK alone to own stdout.

- [ ] **Step 6: Run the complete MCP suite and commit**

Run: `uv run pytest -q`

Expected: all MCP unit and protocol tests pass.

```powershell
git add integrations/mcp
git commit -m "feat: expose read-only Open Ephys MCP tools"
```

### Task 3: Portable Open Ephys operator Skill

**Files:**
- Create: `integrations/skills/open-ephys-operator/SKILL.md`
- Create: `integrations/skills/open-ephys-operator/agents/openai.yaml`
- Create: `integrations/skills/open-ephys-operator/references/safety-policy.md`
- Create: `integrations/skills/open-ephys-operator/references/tool-workflows.md`
- Create: `integrations/skills/open-ephys-operator/references/eight-shank-sop.md`
- Create: `integrations/skills/open-ephys-operator/references/failure-recovery.md`
- Create: `integrations/skills/open-ephys-operator/references/reporting-schema.md`
- Create: `integrations/skills/open-ephys-operator/scripts/validate-session-manifest.py`
- Create: `integrations/skills/open-ephys-operator/tests/test_validate_session_manifest.py`

**Interfaces:**
- Produces: repo-scoped `open-ephys-operator` Skill with a declared dependency on the `open-ephys-agent` stdio MCP server.
- Produces: deterministic manifest validator that accepts observation reports and rejects false success, missing evidence, unknown fields, invalid shank indices, and embedded secrets.

- [ ] **Step 1: Initialize the skill with the official skill creator script**

Run `init_skill.py open-ephys-operator --path integrations/skills --resources scripts,references` with explicit display name, short description, and `$open-ephys-operator` default prompt.

- [ ] **Step 2: Write failing manifest-validator tests**

Cover a valid observation report and rejection of `status=SUCCESS` without artifact evidence, shank outside 1-8, bearer/token-like fields, missing native session/revision, and unknown top-level fields.

- [ ] **Step 3: Implement and run the deterministic validator**

Run: `python integrations/skills/open-ephys-operator/scripts/validate-session-manifest.py fixture.json` and the pytest suite. Return exit 0 only for a schema-valid, internally consistent report.

- [ ] **Step 4: Write concise Skill and one-level references**

Keep `SKILL.md` under 500 lines. Require identity -> capabilities -> status -> preflight ordering, stop on every fail-closed condition, prohibit mutation and port 37497, and use the scientist report schema. Mark the eight-shank SOP as observation/planning-only in this slice.

- [ ] **Step 5: Validate with `quick_validate.py` and commit**

Run: `quick_validate.py integrations/skills/open-ephys-operator`

Expected: `Skill is valid!`

```powershell
git add integrations/skills/open-ephys-operator
git commit -m "feat: add portable Open Ephys operator skill"
```

### Task 4: Windows launch/config adapters and release packaging

**Files:**
- Create: `tools/windows/Start-OpenEphysAgentMcp.ps1`
- Create: `tools/windows/New-OpenEphysAgentMcpConfig.ps1`
- Create: `Tests/AgentContracts/test_mcp_skill_packaging_source.py`
- Modify: `tools/windows/New-AgentRuntimePackage.ps1`
- Modify: `tools/windows/Invoke-AgentChecks.ps1`

**Interfaces:**
- Produces: token-preserving MCP launcher that locates the bundled executable/module and writes no token.
- Produces: deterministic Codex and Claude stdio configuration snippets without editing either user's config.
- Extends: runtime package with `mcp/`, `skills/open-ephys-operator/`, and their hashes.

- [ ] **Step 1: Write failing packaging/config contracts**

Require stdio, process-environment token forwarding by variable name only, no token literal, no 37497, package inclusion, uv lock, Skill tree, and both vendor snippets.

- [ ] **Step 2: Implement adapters and package inclusion**

Keep installation reversible: print/write snippets only to a user-selected output path and never mutate global Codex or Claude configuration automatically.

- [ ] **Step 3: Run all checks and commit**

Run: `pwsh -NoProfile -File tools/windows/Invoke-AgentChecks.ps1` and `uv run pytest -q`.

Expected: all native, source-contract, gateway, MCP, and Skill tests pass.

### Task 5: Real fork MCP acceptance and evidence

**Files:**
- Create: `docs/agent/evidence/mcp-readonly-live-2026-07-16.json`
- Modify: `docs/agent/transport-control-contract-v0.0.1.md`

**Interfaces:**
- Consumes: packaged fork, one-time token, packaged MCP server, and official SDK client.
- Produces: versioned evidence binding process arguments, endpoint listener, MCP handshake, exact tool/resource allowlists, outputs, zero mutation, and cleanup.

- [ ] **Step 1: Verify manifest hashes and launch the packaged fork in isolation**

Require no other Open Ephys process/listener, fresh state directory, loopback 38498, no 37497, `--agent-uia-readonly`, and a 384-bit process-local token.

- [ ] **Step 2: Connect through standard MCP stdio and invoke every allowed operation**

Initialize the session, list exactly four tools and four resources, invoke/read all, and record the fork mode/revision before and after.

- [ ] **Step 3: Prove fail-closed behavior**

Run negative clients with missing token, wrong token, non-loopback URL, schema mismatch fake endpoint, unknown tool, and unknown input field. Assert no fork mode or revision change.

- [ ] **Step 4: Close normally, write evidence, rebuild package, and verify every hash**

Require zero remaining Open Ephys processes and zero 37497/38498 listeners.

- [ ] **Step 5: Run final build/checks and push**

Run the MSVC Release build, `Invoke-AgentChecks.ps1`, MCP pytest suite, Skill validator, package hash verifier, and remote HEAD comparison before reporting completion.
