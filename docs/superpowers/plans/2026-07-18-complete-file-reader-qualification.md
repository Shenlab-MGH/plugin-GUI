# Complete File Reader Qualification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and execute a repeatable Windows qualification that proves the Open Ephys Agent can safely complete eight independent File Reader recording blocks with exact state readback, bounded timing, negative controls, restart recovery, and independently verified evidence.

**Architecture:** Two version-controlled PowerShell tools create an immutable isolated run and execute the GUI qualification through the fork's typed loopback/UIA surfaces. A Python source-contract test protects the safety and evidence requirements. Runtime evidence remains explicit that eight synthetic blocks do not prove Neuropixels preset or physical-shank selection.

**Tech Stack:** PowerShell 7, Windows UI Automation, Open Ephys GUI 1.0.2 fork, Python contract tests, CTest, SHA-256 manifests.

## Global Constraints

- Derive a manifest-bound 120-second source from bundled File Reader data so no five-second source-loop boundary can occur; do not initialize Neuropixels or OneBox hardware.
- Use a unique local-SSD run root under `C:\OE-Agent-Simulation`.
- Native HTTP port 37497 must remain disabled; the agent endpoint must bind only to `127.0.0.1` on a non-37497 port.
- Generate a fresh 384-bit token only in the launched process environment and never persist it.
- Every block must follow `IDLE -> ACQUIRE -> RECORD -> ACQUIRE -> IDLE` with terminal UIA receipt and API readback.
- Every block gets an exact independent directory; collisions and stale revisions fail closed.
- Target duration is 10 seconds per block, minimum accepted persisted duration is 10 seconds, maximum is 12 seconds.
- Closed payloads must be stable for three observations spanning at least 10 seconds.
- Require monotonic contiguous `sample_numbers.npy` and finite monotonic 40-kHz `timestamps.npy`, verified with pinned official `open-ephys-python-tools==1.0.1`.
- File Reader evidence may qualify technical integrity only. `NEUROPIXELS_SIM_CAPABILITY=UNVERIFIED` and `EIGHT_SHANK_CLAIM=PROHIBITED` are mandatory.

---

### Task 1: Version-controlled qualification contract

**Files:**
- Create: `Tests/AgentContracts/test_file_reader_qualification_source.py`
- Create: `tools/windows/New-FileReaderSimulationRun.ps1`
- Create: `tools/windows/Invoke-FileReaderGuiQualification.ps1`
- Modify: `tools/windows/Invoke-AgentChecks.ps1`

**Interfaces:**
- Consumes: packaged `Start-IsolatedAgentRuntime.ps1`, `Test-IsolatedAgentRuntime.ps1`, `Test-AgentAccessibility.ps1`.
- Produces: immutable run manifest and `file-reader-gui-qualification-result.json`.

- [ ] **Step 1: Write the failing source-contract test**

Assert both scripts exist and require eight blocks, duration bounds, 384-bit ephemeral token, exact directory collision checks, stale-revision negative control, three stability observations spanning ten seconds, 16-channel/40-kHz structural validation, independent SHA-256 hashes, and explicit scientific limitations.

- [ ] **Step 2: Run the test and verify RED**

Run: `python Tests/AgentContracts/test_file_reader_qualification_source.py`

Expected: failure because the repository qualification scripts do not exist.

- [ ] **Step 3: Add the minimal scripts and register the contract**

Port the proven local harness, parameterize it for exactly eight blocks and 10-second qualification timing, refresh `FileInfo` on every growth poll, add negative controls, and emit an audit event for every transition.

- [ ] **Step 4: Run focused and complete checks**

Run:

```powershell
python Tests\AgentContracts\test_file_reader_qualification_source.py
pwsh -NoProfile -File tools\windows\Invoke-AgentChecks.ps1
```

Expected: focused contract passes; 18 AgentCore, 448 gateway, 23 HTTP/security, MCP/skill, and all source contracts pass.

- [ ] **Step 5: Commit**

```powershell
git add Tests/AgentContracts/test_file_reader_qualification_source.py tools/windows/New-FileReaderSimulationRun.ps1 tools/windows/Invoke-FileReaderGuiQualification.ps1 tools/windows/Invoke-AgentChecks.ps1
git commit -m "test(agent): add complete File Reader qualification"
```

### Task 2: Build and package the qualification candidate

**Files:**
- Verify: `Build-runtime/Release/open-ephys.exe`
- Produce: `D:\cong\artifacts\open-ephys-agent-v0.0.1\<commit>-windows-x64-runtime`

**Interfaces:**
- Consumes: clean committed source and Release build.
- Produces: 46-file package plus SHA-256 `RUN-MANIFEST.json`.

- [ ] **Step 1: Build Release and execute all CTest tests**

Run:

```powershell
& 'C:\Program Files\CMake\bin\cmake.exe' --build Build-runtime --config Release --parallel
pwsh -NoProfile -File tools\windows\Invoke-OeTests.ps1
```

Expected: Release exit 0 and 29/29 CTest tests pass.

- [ ] **Step 2: Package from a clean worktree**

Run `New-AgentRuntimePackage.ps1` with the current commit-named destination and verify every listed SHA-256 independently.

### Task 3: Eight-block real GUI qualification

**Files:**
- Produce: `C:\OE-Agent-Simulation\OE-FR-QUAL-<timestamp>-<nonce>\RUN-MANIFEST.json`
- Produce: `...\evidence\file-reader-gui-qualification-result.json`
- Produce: eight independent `BLOCK_01` through `BLOCK_08` native recording trees.

**Interfaces:**
- Consumes: verified package and unique isolated run root.
- Produces: runtime, UIA, transition, timing, filesystem, structure, and hash evidence.

- [ ] **Step 1: Prove a clean precondition**

Verify no `open-ephys` process and no listener on 37497 or the selected agent port.

- [ ] **Step 2: Create and verify the immutable run**

Run the repository `New-FileReaderSimulationRun.ps1` with `BlockCount 8`, `TargetSeconds 10`, `MinimumSeconds 10`, and `MaximumSeconds 12`.

- [ ] **Step 3: Execute negative controls before mutation**

Prove missing/wrong token rejection, native port 37497 absence, stale revision rejection, duplicate request idempotency, changed-payload ID conflict, and directory collision rejection without leaving RECORD or creating an unbound recording.

- [ ] **Step 4: Execute eight recording blocks**

For every block prepare the exact directory, obtain UIA terminal receipts for all four state changes, require API mode/revision readback, record for the bounded duration, bind exactly one native recording unit, then require three stable payload checks spanning at least ten seconds.

- [ ] **Step 5: Validate each block independently**

Require `continuous.dat`, `structure.oebin`, and `settings.xml`; 16 channels at 40 kHz; frame alignment; duration 10-12 seconds; unique directory and SHA-256; no growing file; and no duplicate/unresolved native unit.

### Task 4: Restart and independent audit

**Files:**
- Append: run evidence JSON restart section.
- Verify: package and all eight payload trees read-only.

**Interfaces:**
- Consumes: closed, final-IDLE eight-block run.
- Produces: restart recovery and independent audit result.

- [ ] **Step 1: Close only after IDLE is independently confirmed**

Use Windows Computer control to close the GUI and verify process/listener disappearance.

- [ ] **Step 2: Relaunch from the same isolated state**

Use a fresh ephemeral token, require the exact executable/configuration/state paths, confirm immediate `READY/IDLE`, matching graph/config hash, loopback-only agent listener, and no native 37497 listener.

- [ ] **Step 3: Close the recovered IDLE instance**

Use Windows Computer control and verify zero remaining process/listener.

- [ ] **Step 4: Independently recompute evidence**

Without trusting the qualification script's `pass`, enumerate exactly eight block directories, recompute file sizes/frames/durations/SHA-256, parse every structure file, verify stable closure, and compare the package manifest.

- [ ] **Step 5: Report qualification boundary**

Report `FILE_READER_EIGHT_BLOCK_QUALIFICATION=PASS` only if every item above passes. Always report `NEUROPIXELS_SIM_CAPABILITY=UNVERIFIED`, `EIGHT_PRESET_SIM_ACCEPTANCE=BLOCKED`, `EIGHT_SHANK_CLAIM=PROHIBITED`, and `SCIENTIFIC_SIGNAL_QC=NEEDS_REVIEW` until hardware-specific evidence exists.
