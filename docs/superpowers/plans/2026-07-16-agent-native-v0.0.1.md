# Open Ephys Agent-Native v0.0.1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce a verified Windows x64 build of Open Ephys GUI v1.0.2 with a disabled-by-default, test-driven agent command seam, read-only state model, and accessible Play/Record controls.

**Architecture:** Keep real-time acquisition, ProcessorGraph, and Record Node behavior unchanged. Add small C++17 value types and dispatcher interfaces outside real-time callbacks, route ControlPanel mouse and accessibility actions through the same internal seam, and keep networking/workflow/Agent execution outside the Open Ephys process.

**Tech Stack:** C++17, JUCE 8 bundled with Open Ephys, CMake 3.31.8, Visual Studio 2022 Build Tools 17.14.36, GoogleTest 1.12.1, Windows UI Automation through JUCE accessibility.

## Global Constraints

- Base every change on official tag `v1.0.2`, commit `c91afebcfb0678a667fb93f6312ed33c56ec640f`.
- Do not start, stop, close, reconfigure, or attach to the installed laboratory Open Ephys process.
- Do not access real acquisition hardware during v0.0.1 implementation.
- Do not add network listeners, HTTP mutation endpoints, model inference, video encoding, database I/O, or blocking waits to the host.
- Do not place agent code in `process()`, data-thread callbacks, Record Engine writes, or the audio callback.
- Preserve the existing GUI recording synchronization warning and all existing ControlPanel validation behavior.
- Follow red-green-refactor: no production behavior without a failing test first.
- Build only Windows x64 Release for acceptance; Debug may be used for diagnosis.
- The fork remains experimental and is not approved for research acquisition.

---

### Task 1: Reproducible Windows Toolchain and Baseline Evidence

**Files:**
- Create: `tools/windows/Invoke-OeConfigure.ps1`
- Create: `tools/windows/Invoke-OeBuild.ps1`
- Create: `tools/windows/Invoke-OeTests.ps1`
- Create: `tools/windows/Get-BuildInventory.ps1`
- Create: `docs/build/baseline-v1.0.2.md`

**Interfaces:**
- Consumes: Official repository source at commit `c91afeb`.
- Produces: Repeatable configure, build, test, and inventory commands used by every later task.

- [ ] **Step 1: Install the pinned toolchain**

Install Visual Studio Build Tools 17.14.36 with
`Microsoft.VisualStudio.Workload.VCTools --includeRecommended`, and install
Kitware CMake 3.31.8. Record installer exit codes and logs. Treat exit code
3010 as installed/restart-required rather than build failure.

- [ ] **Step 2: Verify the toolchain**

Run:

```powershell
cmake --version
& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' `
  -latest -products * `
  -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
  -property installationPath
cmd /d /s /c """C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"" -arch=amd64 && cl /Bv && msbuild -version"
```

Expected: CMake 3.31.8, VS Build Tools installation path, MSVC x64 compiler,
and MSBuild version output.

- [ ] **Step 3: Write the inventory script**

`Get-BuildInventory.ps1` must emit JSON containing:

```json
{
  "source_commit": "",
  "source_describe": "",
  "cmake_version": "",
  "generator": "Visual Studio 17 2022",
  "architecture": "x64",
  "visual_studio_installation": "",
  "msvc_version": "",
  "windows_sdk_versions": [],
  "configuration": "Release"
}
```

- [ ] **Step 4: Write configure/build/test wrappers**

The configure wrapper must run from the repository root:

```powershell
cmake -S . -B Build -G 'Visual Studio 17 2022' -A x64 -DBUILD_TESTS=ON
```

The build wrapper must run:

```powershell
cmake --build Build --config Release --parallel
```

The test wrapper must run:

```powershell
ctest --test-dir Build -C Release --output-on-failure
```

Each wrapper uses `$ErrorActionPreference = 'Stop'`, checks `$LASTEXITCODE`,
and exits non-zero on failure.

- [ ] **Step 5: Configure the untouched baseline**

Run:

```powershell
pwsh -File tools/windows/Invoke-OeConfigure.ps1
```

Expected: `Build/open-ephys-GUI.sln` exists and configure exits 0.

- [ ] **Step 6: Build the untouched baseline**

Run:

```powershell
pwsh -File tools/windows/Invoke-OeBuild.ps1
```

Expected: `Build/Release/open-ephys.exe` exists and build exits 0.

- [ ] **Step 7: Run official unit tests**

Run:

```powershell
pwsh -File tools/windows/Invoke-OeTests.ps1
```

Expected: CTest exits 0. Record exact test count; do not predict it.

- [ ] **Step 8: Record baseline evidence**

Write `docs/build/baseline-v1.0.2.md` with the exact commands, exit codes,
toolchain inventory, test result, binary path, and SHA-256 from:

```powershell
Get-FileHash Build\Release\open-ephys.exe -Algorithm SHA256
```

- [ ] **Step 9: Commit**

```powershell
git add tools/windows docs/build
git commit -m "build: add reproducible Windows baseline"
```

---

### Task 2: Agent Command Value Types

**Files:**
- Create: `Source/Agent/AgentCommand.h`
- Create: `Source/Agent/AgentCommand.cpp`
- Create: `Tests/Agent/CMakeLists.txt`
- Create: `Tests/Agent/AgentCommandTests.cpp`
- Modify: `Source/CMakeLists.txt`
- Modify: `Tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `enum class AgentCommandType`
  - `enum class AgentCommandOrigin`
  - `struct AgentCommand`
  - `class AgentCommandDispatcher`

- [ ] **Step 1: Write the failing command-value tests**

Test that acquisition and recording commands preserve their type and origin:

```cpp
TEST (AgentCommandTests, PreservesTypeAndOrigin)
{
    AgentCommand command {
        AgentCommandType::requestRecordingToggle,
        AgentCommandOrigin::accessibility
    };

    EXPECT_EQ (command.type, AgentCommandType::requestRecordingToggle);
    EXPECT_EQ (command.origin, AgentCommandOrigin::accessibility);
}
```

- [ ] **Step 2: Run the Agent test target and verify RED**

Run:

```powershell
cmake --build Build --config Release --target Agent_tests
```

Expected: compilation fails because `AgentCommand.h` does not exist.

- [ ] **Step 3: Add minimal command types**

Implement:

```cpp
enum class AgentCommandType
{
    requestAcquisitionToggle,
    requestRecordingToggle
};

enum class AgentCommandOrigin
{
    mouse,
    keyboard,
    accessibility,
    internal
};

struct AgentCommand
{
    AgentCommandType type;
    AgentCommandOrigin origin;
};

class AgentCommandDispatcher
{
public:
    virtual ~AgentCommandDispatcher() = default;
    virtual void dispatch (const AgentCommand&) = 0;
};
```

- [ ] **Step 4: Build and run the Agent tests**

Run:

```powershell
cmake --build Build --config Release --target Agent_tests
ctest --test-dir Build -C Release -R Agent_tests --output-on-failure
```

Expected: Agent tests pass.

- [ ] **Step 5: Run the complete test suite**

```powershell
pwsh -File tools/windows/Invoke-OeTests.ps1
```

Expected: all registered tests pass.

- [ ] **Step 6: Commit**

```powershell
git add Source/Agent Source/CMakeLists.txt Tests/Agent Tests/CMakeLists.txt
git commit -m "feat: add agent command contract"
```

---

### Task 3: Read-Only Agent State Store

**Files:**
- Create: `Source/Agent/AgentState.h`
- Create: `Source/Agent/AgentState.cpp`
- Create: `Tests/Agent/AgentStateTests.cpp`
- Modify: `Tests/Agent/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `enum class AgentObservedMode { idle, acquire, record, unknown }`
  - `struct AgentStateSnapshot`
  - `class AgentStateStore`
  - `AgentStateSnapshot AgentStateStore::observe(AgentObservedMode mode)`

- [ ] **Step 1: Write failing state and revision tests**

Cover:

```cpp
TEST (AgentStateTests, StartsUnknownAtRevisionZero);
TEST (AgentStateTests, IncrementsRevisionWhenModeChanges);
TEST (AgentStateTests, KeepsRevisionWhenModeDoesNotChange);
```

The store constructor accepts the GUI version string. The first snapshot is
`unknown`, revision `0`.

- [ ] **Step 2: Run and verify RED**

```powershell
cmake --build Build --config Release --target Agent_tests
```

Expected: missing `AgentState` symbols.

- [ ] **Step 3: Implement the minimal state store**

Use an in-memory value object with no locks, timers, network calls, or I/O.
`observe()` increments the revision only when the new mode differs from the
current mode.

- [ ] **Step 4: Run targeted and complete tests**

```powershell
cmake --build Build --config Release --target Agent_tests
ctest --test-dir Build -C Release -R Agent_tests --output-on-failure
pwsh -File tools/windows/Invoke-OeTests.ps1
```

Expected: all tests pass.

- [ ] **Step 5: Commit**

```powershell
git add Source/Agent Tests/Agent
git commit -m "feat: add read-only agent state model"
```

---

### Task 4: ControlPanel Command Dispatch Seam

**Files:**
- Create: `Source/Agent/ControlPanelCommandRouter.h`
- Create: `Source/Agent/ControlPanelCommandRouter.cpp`
- Create: `Tests/Agent/ControlPanelCommandRouterTests.cpp`
- Modify: `Source/UI/ControlPanel.h`
- Modify: `Source/UI/ControlPanel.cpp`
- Modify: `Tests/Agent/CMakeLists.txt`

**Interfaces:**
- Consumes: `AgentCommand`, `AgentCommandDispatcher`.
- Produces:
  - `ControlPanelCommandRouter::requestAcquisitionToggle(origin)`
  - `ControlPanelCommandRouter::requestRecordingToggle(origin)`

- [ ] **Step 1: Write failing router tests**

Use a recording test dispatcher and prove:

```cpp
router.requestAcquisitionToggle (AgentCommandOrigin::mouse);
EXPECT_EQ (dispatcher.last.type, AgentCommandType::requestAcquisitionToggle);
EXPECT_EQ (dispatcher.last.origin, AgentCommandOrigin::mouse);
```

Repeat for recording and accessibility origin.

- [ ] **Step 2: Run and verify RED**

Expected: router class is missing.

- [ ] **Step 3: Implement the minimal router**

The router only constructs typed commands and calls the injected dispatcher.
It must not reference `ProcessorGraph`, Record Node, audio callbacks, HTTP, or
filesystem APIs.

- [ ] **Step 4: Add a ControlPanel dispatcher adapter**

Create a private ControlPanel implementation of `AgentCommandDispatcher`.
For `requestAcquisitionToggle`, call the same existing branch formerly used by
the Play button. For `requestRecordingToggle`, call the same existing branch
formerly used by Record. Move existing validation code into focused private
methods:

```cpp
void handleAcquisitionToggleRequest();
void handleRecordingToggleRequest();
```

Do not alter any validation, warning, toggle-state, or start/stop behavior.

- [ ] **Step 5: Route mouse clicks through the seam**

In `ControlPanel::buttonClicked`, Play and Record submit commands with
`AgentCommandOrigin::mouse`. All other buttons remain unchanged.

- [ ] **Step 6: Run targeted and complete tests**

```powershell
cmake --build Build --config Release --target Agent_tests
ctest --test-dir Build -C Release -R Agent_tests --output-on-failure
pwsh -File tools/windows/Invoke-OeTests.ps1
```

Expected: all tests pass.

- [ ] **Step 7: Commit**

```powershell
git add Source/Agent Source/UI/ControlPanel.* Tests/Agent
git commit -m "refactor: route transport controls through agent commands"
```

---

### Task 5: Accessible Play and Record Controls

**Files:**
- Create: `Source/Agent/TransportAccessibility.h`
- Create: `Source/Agent/TransportAccessibility.cpp`
- Create: `Tests/Agent/TransportAccessibilityTests.cpp`
- Modify: `Source/UI/ControlPanel.h`
- Modify: `Source/UI/ControlPanel.cpp`
- Modify: `Tests/Agent/CMakeLists.txt`

**Interfaces:**
- Consumes: `ControlPanelCommandRouter`.
- Produces:
  - Play accessibility title `Start or stop acquisition`
  - Record accessibility title `Start or stop recording`
  - accessibility actions that submit typed commands

- [ ] **Step 1: Write failing metadata tests**

Instantiate Play and Record controls under a JUCE `MessageManagerLock` and
assert:

```cpp
EXPECT_FALSE (handler->getTitle().isEmpty());
EXPECT_TRUE (handler->getCurrentState().isCheckable());
```

Also verify the action submits the correct command with origin
`AgentCommandOrigin::accessibility`.

- [ ] **Step 2: Run and verify RED**

Expected: the custom accessibility handlers/actions are missing.

- [ ] **Step 3: Implement handlers**

Override `createAccessibilityHandler()` for `PlayButton` and `RecordButton`.
Use JUCE roles/actions and expose localized title, description, help text,
enabled state, checkable state, checked state, and focus.

- [ ] **Step 4: Route accessibility actions through the command router**

The accessibility callback submits a command. It must not call
`startAcquisition`, `stopAcquisition`, `startRecording`, `stopRecording`, or
`ProcessorGraph` directly.

- [ ] **Step 5: Notify state changes**

When Play or Record toggle state changes through existing ControlPanel state
updates, notify the accessibility handler with the appropriate value/state
change event. Do not poll.

- [ ] **Step 6: Run targeted and complete tests**

```powershell
cmake --build Build --config Release --target Agent_tests
ctest --test-dir Build -C Release -R Agent_tests --output-on-failure
pwsh -File tools/windows/Invoke-OeTests.ps1
```

Expected: all tests pass.

- [ ] **Step 7: Commit**

```powershell
git add Source/Agent Source/UI/ControlPanel.* Tests/Agent
git commit -m "feat: expose accessible transport controls"
```

---

### Task 6: Feature Gate and Build Identity

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `Source/CMakeLists.txt`
- Create: `Source/Agent/AgentFeatureConfig.h.in`
- Create: `Tests/Agent/AgentFeatureConfigTests.cpp`
- Create: `docs/build/agent-v0.0.1.md`

**Interfaces:**
- Produces: CMake option `OE_AGENT_NATIVE_FEATURES`, default `OFF`.

- [ ] **Step 1: Write a failing feature-gate test**

The enabled build exposes:

```cpp
constexpr bool agentNativeFeaturesEnabled = true;
```

The disabled build exposes false and retains default ControlPanel construction.

- [ ] **Step 2: Run and verify RED**

Expected: generated feature configuration does not exist.

- [ ] **Step 3: Add the CMake option**

Add:

```cmake
option(OE_AGENT_NATIVE_FEATURES "Enable experimental agent-native controls" OFF)
```

Generate `AgentFeatureConfig.h` with `configure_file`. Compile agent-specific
ControlPanel integration only when the option is enabled; keep pure value-type
tests available in test builds.

- [ ] **Step 4: Verify disabled build**

Use a fresh build tree:

```powershell
cmake -S . -B Build-disabled -G 'Visual Studio 17 2022' -A x64 `
  -DOE_DONT_CHECK_BUILD_PATH=TRUE -DBUILD_TESTS=ON `
  -DOE_AGENT_NATIVE_FEATURES=OFF
cmake --build Build-disabled --config Release --parallel
ctest --test-dir Build-disabled -C Release --output-on-failure
```

Expected: build and tests pass.

- [ ] **Step 5: Verify enabled build**

```powershell
cmake -S . -B Build-agent -G 'Visual Studio 17 2022' -A x64 `
  -DOE_DONT_CHECK_BUILD_PATH=TRUE -DBUILD_TESTS=ON `
  -DOE_AGENT_NATIVE_FEATURES=ON
cmake --build Build-agent --config Release --parallel
ctest --test-dir Build-agent -C Release --output-on-failure
```

Expected: build and tests pass.

- [ ] **Step 6: Record build identity**

Write `docs/build/agent-v0.0.1.md` containing source commit, complete diff base,
toolchain JSON, test results, enabled and disabled binary SHA-256 values, and
the statement that the build is not approved for experiments.

- [ ] **Step 7: Commit**

```powershell
git add CMakeLists.txt Source/CMakeLists.txt Source/Agent Tests/Agent docs/build
git commit -m "build: gate agent-native features"
```

---

### Task 7: Non-Hardware UIA Smoke Verification

**Files:**
- Create: `tools/accessibility/Test-TransportAccessibility.ps1`
- Create: `docs/verification/accessibility-v0.0.1.md`

**Interfaces:**
- Consumes: enabled Release build.
- Produces: machine-readable UIA smoke evidence without acquisition or recording.

- [ ] **Step 1: Write the smoke verifier**

The PowerShell verifier launches the fork with a safe simulation/default
configuration only after confirming no installed laboratory Open Ephys process
shares the target executable path. It finds the fork process by exact path,
queries the UIA tree, and asserts that accessible Play and Record elements have
non-empty names, correct enabled state, and supported invoke/toggle patterns.

The verifier must not invoke Play or Record.

- [ ] **Step 2: Run the verifier**

```powershell
pwsh -File tools/accessibility/Test-TransportAccessibility.ps1 `
  -Executable Build-agent\Release\open-ephys.exe `
  -ObserveOnly
```

Expected: PASS for element discovery and metadata; zero transport mutations.

- [ ] **Step 3: Close only the fork process**

Use the exact process ID returned by the verifier. Confirm the laboratory
Open Ephys executable/process was never targeted.

- [ ] **Step 4: Write verification evidence**

Record:

- fork executable hash and path;
- discovered UIA names, roles, patterns, and bounds;
- process IDs and exact executable paths;
- confirmation that Play/Record were not invoked;
- limitations, including absence of real hardware validation.

- [ ] **Step 5: Commit**

```powershell
git add tools/accessibility docs/verification
git commit -m "test: verify transport accessibility metadata"
```

---

### Task 8: Final v0.0.1 Verification and Private Push

**Files:**
- Modify: `docs/build/agent-v0.0.1.md`
- Modify: `docs/verification/accessibility-v0.0.1.md`

**Interfaces:**
- Produces: Auditable v0.0.1 evidence and private branch.

- [ ] **Step 1: Verify repository provenance**

```powershell
git merge-base --is-ancestor c91afeb HEAD
git diff --check c91afeb..HEAD
git status --short
git remote -v
```

Expected: official base is an ancestor, no whitespace errors, clean worktree,
`origin` is the private mirror, and `upstream` is official.

- [ ] **Step 2: Run both full build/test matrices**

Run disabled and enabled Release configure/build/CTest commands from Task 6.
Expected: all builds and tests exit 0.

- [ ] **Step 3: Re-run observe-only UIA verification**

Run the exact Task 7 verifier. Expected: accessible metadata present and no
transport action invoked.

- [ ] **Step 4: Hash deliverables**

```powershell
Get-FileHash Build-disabled\Release\open-ephys.exe -Algorithm SHA256
Get-FileHash Build-agent\Release\open-ephys.exe -Algorithm SHA256
```

- [ ] **Step 5: Audit scope**

Confirm every v0.0.1 requirement in the design document has direct evidence.
Explicitly list unimplemented public API, Agent lease, Neuropixels control,
workflow replay, video capture, and real-hardware validation.

- [ ] **Step 6: Push the implementation branch**

```powershell
git push -u origin agent/v0.0.1
```

- [ ] **Step 7: Tag only after all evidence passes**

```powershell
git tag -a agent-v0.0.1 -m "Experimental Open Ephys agent-native v0.0.1"
git push origin agent-v0.0.1
```

Do not create or push the tag if any build, test, UIA check, or provenance
check fails.
