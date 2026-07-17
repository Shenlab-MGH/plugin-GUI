# UIA Read-Only Foundation v0.0.1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an explicitly enabled, read-only Windows UI Automation branch to the Open Ephys fork with exact stable AutomationIds, authoritative transport values, and no Invoke or Toggle capability.

**Architecture:** Extend runtime isolation options with `--agent-uia-readonly`. When enabled in GUI mode, make the document window an accessibility root, keep the ordinary Open Ephys content subtree inaccessible, and attach a transparent `AgentAccessibilityBridge` containing exactly two semantic transport nodes. Patch the bundled JUCE Windows provider to prefer non-empty `ComponentID` values for AutomationId and verify the result with Windows' native UI Automation API.

**Tech Stack:** C++17, JUCE 8 bundled with Open Ephys GUI v1.0.2, Windows UI Automation COM through PowerShell/.NET `UIAutomationClient`, Python source-contract tests, CMake/MSVC 2022 Release.

## Global Constraints

- Base all changes on official Open Ephys GUI `v1.0.2` through the existing fork branch.
- UIA remains disabled unless `--agent-uia-readonly` is present exactly once.
- UIA v0.0.1 is observation-only and exposes no Press, Invoke, or Toggle action.
- Transport values come from `AgentTransportEndpoint::snapshot()`, never button state.
- The ordinary Open Ephys editor and plugin component subtree remains inaccessible.
- `UNKNOWN` remains an explicit value and is never coerced to inactive.
- UIA inspection must not change transport mode or revision.
- Existing manual GUI behavior, native HTTP defaults, plugin defaults, and headless behavior remain unchanged without the new option.
- No coordinate clicking or image recognition is part of acceptance.

---

### Task 1: Parse an explicit read-only UIA runtime option

**Files:**
- Modify: `Source/Agent/RuntimeOptions.h`
- Modify: `Source/Agent/RuntimeOptions.cpp`
- Modify: `Tests/AgentCore/RuntimeOptionsTests.cpp`

**Interfaces:**
- Produces: `RuntimeIsolationOptions::enableAgentUiaReadOnly : bool`
- Consumes: existing `RuntimeOptions::parse(const std::vector<std::string>&)`

- [ ] **Step 1: Write the failing runtime-option tests**

Add assertions that the default is disabled, the explicit option enables it,
and duplication fails closed:

```cpp
require (! defaults.options.enableAgentUiaReadOnly,
         "Agent UIA must remain disabled by default");

const auto uia = RuntimeOptions::parse ({ "--agent-uia-readonly" });
require (uia.ok(), "The read-only UIA option must parse");
require (uia.options.enableAgentUiaReadOnly,
         "The read-only UIA option must be preserved");

require (! RuntimeOptions::parse ({
             "--agent-uia-readonly",
             "--agent-uia-readonly"
         }).ok(),
         "Duplicate UIA switches must fail closed");
```

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```powershell
pwsh -NoProfile -File tools\windows\Invoke-AgentCoreTests.ps1
```

Expected: compilation fails because `enableAgentUiaReadOnly` does not exist.

- [ ] **Step 3: Implement the minimal option**

Add the field:

```cpp
bool enableAgentUiaReadOnly = false;
```

Add a `sawAgentUiaReadOnly` duplicate guard and parse:

```cpp
else if (argument == "--agent-uia-readonly")
{
    if (! consumeOnce (
            sawAgentUiaReadOnly,
            result,
            "--agent-uia-readonly"))
        return result;
    result.options.enableAgentUiaReadOnly = true;
}
```

- [ ] **Step 4: Run the focused test and verify GREEN**

Run the same command. Expected: all existing Agent core tests pass.

- [ ] **Step 5: Commit**

```powershell
git add Source/Agent/RuntimeOptions.h Source/Agent/RuntimeOptions.cpp Tests/AgentCore/RuntimeOptionsTests.cpp
git commit -m "feat: add explicit read-only UIA runtime option"
```

---

### Task 2: Define pure authoritative UIA transport values

**Files:**
- Create: `Source/Agent/AgentAccessibilityState.h`
- Create: `Source/Agent/AgentAccessibilityState.cpp`
- Create: `Tests/AgentCore/AgentAccessibilityStateTests.cpp`
- Modify: `tools/windows/Invoke-AgentCoreTests.ps1`
- Modify: `Source/Agent/CMakeLists.txt`

**Interfaces:**
- Produces: `AgentAccessibilityValue { off, on, unknown }`
- Produces: `AgentAccessibilityState::acquisition(AgentObservedMode)`
- Produces: `AgentAccessibilityState::recording(AgentObservedMode)`
- Produces: `AgentAccessibilityState::toString(AgentAccessibilityValue)`

- [ ] **Step 1: Write the failing pure C++ test**

The test must assert this complete truth table:

```cpp
require (acquisition (idle) == off, "IDLE acquisition must be OFF");
require (acquisition (acquire) == on, "ACQUIRE acquisition must be ON");
require (acquisition (record) == on, "RECORD acquisition must be ON");
require (acquisition (unknown) == unknownValue,
         "UNKNOWN acquisition must remain UNKNOWN");
require (recording (idle) == off, "IDLE recording must be OFF");
require (recording (acquire) == off, "ACQUIRE recording must be OFF");
require (recording (record) == on, "RECORD recording must be ON");
require (recording (unknown) == unknownValue,
         "UNKNOWN recording must remain UNKNOWN");
require (toString (off) == "OFF", "OFF text must be stable");
require (toString (on) == "ON", "ON text must be stable");
require (toString (unknownValue) == "UNKNOWN",
         "UNKNOWN text must be stable");
```

- [ ] **Step 2: Register the new test executable and verify RED**

Add an `Invoke-CompileAndRun` entry to `Invoke-AgentCoreTests.ps1`, then run it.
Expected: compile failure because `AgentAccessibilityState.h` is missing.

- [ ] **Step 3: Implement the pure mapping**

Use exhaustive `switch` statements with no dependency on JUCE or GUI buttons.
Return `unknown` from the defensive fallthrough.

- [ ] **Step 4: Add production sources to `Source/Agent/CMakeLists.txt` and verify GREEN**

Run:

```powershell
pwsh -NoProfile -File tools\windows\Invoke-AgentCoreTests.ps1
```

Expected: the new test and all existing tests pass.

- [ ] **Step 5: Commit**

```powershell
git add Source/Agent/AgentAccessibilityState.* Tests/AgentCore/AgentAccessibilityStateTests.cpp Source/Agent/CMakeLists.txt tools/windows/Invoke-AgentCoreTests.ps1
git commit -m "feat: define authoritative UIA transport values"
```

---

### Task 3: Add the three-node read-only semantic accessibility branch

**Files:**
- Create: `Source/Agent/AgentAccessibilityBridge.h`
- Create: `Source/Agent/AgentAccessibilityBridge.cpp`
- Modify: `Source/Agent/CMakeLists.txt`
- Modify: `Source/MainWindow.h`
- Modify: `Source/MainWindow.cpp`
- Modify: `Tests/AgentContracts/test_transport_accessibility_source.py`

**Interfaces:**
- Produces: `AgentAccessibilityBridge(std::shared_ptr<AgentTransportEndpoint>)`
- Consumes: `AgentTransportEndpoint::snapshot() const`
- Consumes: `TransportAccessibilityRegistry`

- [ ] **Step 1: Replace the source-contract expectation with the desired branch**

Require all of the following source fragments before implementation:

```python
require('"oe.agent.root"', BRIDGE,
        "The bridge must expose one stable root")
require("AccessibilityActions {}", BRIDGE,
        "The bridge must expose no actions")
require("AccessibilityTextValueInterface", BRIDGE,
        "Transport nodes must provide a read-only value")
require("isReadOnly() const override", BRIDGE,
        "The UIA value must reject writes")
require("enableAgentUiaReadOnly", MAIN_WINDOW,
        "The branch must be explicitly enabled")
require("ui->setAccessible (false)", MAIN_WINDOW,
        "The ordinary editor subtree must remain hidden")
```

Also require exactly two `addAndMakeVisible` calls inside the bridge constructor
for acquisition and recording children.

- [ ] **Step 2: Run the contract and verify RED**

```powershell
python Tests\AgentContracts\test_transport_accessibility_source.py
```

Expected: failure because `AgentAccessibilityBridge.cpp` is absent.

- [ ] **Step 3: Implement `AgentAccessibilityBridge`**

Create a transparent, non-interactive `Component` with ComponentID
`oe.agent.root`, title `Open Ephys Agent`, and exactly two child components.
Each child:

- has the registry AutomationId as its ComponentID;
- uses `AccessibilityRole::button`;
- returns an empty `AccessibilityActions` object;
- returns a read-only `AccessibilityTextValueInterface`;
- obtains its value from `endpoint->snapshot()` and the pure mapping from Task 2;
- returns `withAccessibleOffscreen()` from `getCurrentState()`;
- ignores `setValueAsString` without changing state;
- paints nothing and intercepts no mouse input.

Use this constructor shape:

```cpp
explicit AgentAccessibilityBridge (
    std::shared_ptr<AgentTransportEndpoint> endpoint);
```

The bridge owns both children by value or `unique_ptr`; no child retains a raw
`ControlPanel*`.

- [ ] **Step 4: Wire the branch only in explicit GUI mode**

Change `MainDocumentWindow` to accept `bool enableAgentUiaReadOnly` and call:

```cpp
setAccessible (enableAgentUiaReadOnly);
```

After creating the normal `UIComponent`, when the option is enabled:

```cpp
ui->setAccessible (false);
agentAccessibilityBridge =
    std::make_unique<AgentAccessibilityBridge> (
        controlPanel->getAgentTransportEndpoint());
documentWindow->addAndMakeVisible (agentAccessibilityBridge.get());
agentAccessibilityBridge->setBounds (0, 0, 1, 1);
```

Leave the document window inaccessible and do not create the bridge when the
option is absent or when running headless.

- [ ] **Step 5: Run source contracts and full Agent checks**

```powershell
python Tests\AgentContracts\test_transport_accessibility_source.py
pwsh -NoProfile -File tools\windows\Invoke-AgentChecks.ps1
```

Expected: all checks pass.

- [ ] **Step 6: Commit**

```powershell
git add Source/Agent/AgentAccessibilityBridge.* Source/Agent/CMakeLists.txt Source/MainWindow.* Tests/AgentContracts/test_transport_accessibility_source.py
git commit -m "feat: expose a read-only Agent UIA branch"
```

---

### Task 4: Make non-empty ComponentID the exact Windows AutomationId

**Files:**
- Modify: `JuceLibraryCode/modules/juce_gui_basics/native/accessibility/juce_AccessibilityElement_windows.cpp`
- Create: `Tests/AgentContracts/test_windows_automation_id_source.py`
- Modify: `tools/windows/Invoke-AgentChecks.ps1`

**Interfaces:**
- Changes: JUCE Windows `getAutomationId(const AccessibilityHandler&)`
- Preserves: existing title-hierarchy fallback for empty ComponentID

- [ ] **Step 1: Write the failing source regression**

The contract must locate `getAutomationId` and require this ordering:

```cpp
const auto componentId =
    handler.getComponent().getComponentID();
if (componentId.isNotEmpty())
    return componentId;
```

It must also require the existing title-hierarchy fallback after this block.

- [ ] **Step 2: Run the contract and verify RED**

```powershell
python Tests\AgentContracts\test_windows_automation_id_source.py
```

Expected: failure because JUCE currently derives every AutomationId from
titles.

- [ ] **Step 3: Implement the minimal JUCE mapping repair**

Insert the ComponentID preference at the start of `getAutomationId`. Do not
change any other UIA property or provider selection.

- [ ] **Step 4: Run all source contracts and verify GREEN**

```powershell
pwsh -NoProfile -File tools\windows\Invoke-AgentChecks.ps1
```

Expected: all C++, Python, gateway, and source-contract tests pass.

- [ ] **Step 5: Commit**

```powershell
git add JuceLibraryCode/modules/juce_gui_basics/native/accessibility/juce_AccessibilityElement_windows.cpp Tests/AgentContracts/test_windows_automation_id_source.py tools/windows/Invoke-AgentChecks.ps1
git commit -m "fix: honor ComponentID for Windows AutomationId"
```

---

### Task 5: Add a real Windows UIA acceptance verifier

**Files:**
- Create: `tools/windows/Test-AgentAccessibility.ps1`
- Create: `Tests/AgentContracts/test_windows_uia_verifier_source.py`
- Modify: `tools/windows/Invoke-AgentChecks.ps1`

**Interfaces:**
- Produces: compact JSON with `pass`, discovered IDs, patterns, values,
  pre/post native revision, and 10,000-read count
- Consumes: `ProcessId`, `AgentPort`, and `OE_AGENT_TOKEN` from the environment

- [ ] **Step 1: Write a failing verifier source contract**

Require the verifier to:

- load `UIAutomationClient`;
- select elements by both process ID and exact AutomationId;
- require root ID `oe.agent.root`;
- require child IDs `oe.transport.acquisition` and
  `oe.transport.recording`;
- query `ValuePattern`, `InvokePattern`, and `TogglePattern`;
- perform exactly 10,000 value reads;
- query authenticated `/v1/status` before and after;
- compare both mode and revision;
- serialize only selected scalar fields.

- [ ] **Step 2: Run the contract and verify RED**

```powershell
python Tests\AgentContracts\test_windows_uia_verifier_source.py
```

Expected: failure because the verifier is missing.

- [ ] **Step 3: Implement the verifier**

Use `.NET` `System.Windows.Automation.AutomationElement` and
`PropertyCondition`/`AndCondition`; do not use screen coordinates. Reject a
missing or shorter-than-32-character `OE_AGENT_TOKEN`. Verify:

```text
root count = 1
root direct child count = 2
acquisition count = 1
recording count = 1
ValuePattern available = true
ValuePattern.IsReadOnly = true
InvokePattern available = false
TogglePattern available = false
pre mode = post mode
pre revision = post revision
read count = 10000
```

Return exit code 0 only when every condition passes.

- [ ] **Step 4: Run source checks and verify GREEN**

```powershell
pwsh -NoProfile -File tools\windows\Invoke-AgentChecks.ps1
```

Expected: all checks pass without launching Open Ephys.

- [ ] **Step 5: Commit**

```powershell
git add tools/windows/Test-AgentAccessibility.ps1 Tests/AgentContracts/test_windows_uia_verifier_source.py tools/windows/Invoke-AgentChecks.ps1
git commit -m "test: add native Windows UIA acceptance verifier"
```

---

### Task 6: Add UIA to the isolated launcher and runtime verifier

**Files:**
- Modify: `tools/windows/Start-IsolatedAgentRuntime.ps1`
- Modify: `tools/windows/Test-IsolatedAgentRuntime.ps1`
- Modify: `Tests/AgentContracts/test_isolated_runtime_launcher_source.py`

**Interfaces:**
- Launcher always passes `--agent-uia-readonly` in the v0.0.1 isolated profile
- Runtime verifier proves the exact argument is present

- [ ] **Step 1: Write failing launcher contract assertions**

Require `--agent-uia-readonly` in both the launch argument list and the runtime
command-line verification checks.

- [ ] **Step 2: Run the contract and verify RED**

```powershell
python Tests\AgentContracts\test_isolated_runtime_launcher_source.py
```

Expected: failure because the option is not yet passed.

- [ ] **Step 3: Add the option and compact verifier field**

Add the launch argument after `--no-user-plugins`. Add JSON check:

```powershell
agent_uia_readonly_argument =
    $commandLine -match '(?:^|\s)--agent-uia-readonly(?:\s|$)'
```

Include this check in the aggregate `pass` result.

- [ ] **Step 4: Run all Agent checks and verify GREEN**

```powershell
pwsh -NoProfile -File tools\windows\Invoke-AgentChecks.ps1
```

Expected: all checks pass.

- [ ] **Step 5: Commit**

```powershell
git add tools/windows/Start-IsolatedAgentRuntime.ps1 tools/windows/Test-IsolatedAgentRuntime.ps1 Tests/AgentContracts/test_isolated_runtime_launcher_source.py
git commit -m "feat: enable read-only UIA in isolated runtime"
```

---

### Task 7: Build, run, and accept the real Windows fork

**Files:**
- Modify: `docs/agent/transport-control-contract-v0.0.1.md`
- Generated evidence: `docs/agent/evidence/uia-readonly-live-2026-07-16.json`
- Generated release root: `D:\cong\artifacts\open-ephys-agent-v0.0.1`; derive the directory name with `$shortCommit = (git rev-parse --short=9 HEAD).Trim()` and `"$shortCommit-windows-x64-runtime"`

**Interfaces:**
- Consumes: `Build-runtime/Release/open-ephys.exe`
- Consumes: isolated launcher, runtime verifier, and UIA verifier

- [ ] **Step 1: Run the complete pre-build suite**

```powershell
pwsh -NoProfile -File tools\windows\Invoke-AgentChecks.ps1
git diff --check
git status --short
```

Expected: all tests pass, no whitespace errors, and only intentional tracked
changes are present.

- [ ] **Step 2: Configure and build a clean Release runtime**

Run:

```powershell
cmake -S . -B Build-runtime -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTS=OFF -DOE_DONT_CHECK_BUILD_PATH=TRUE
cmake --build Build-runtime --config Release --parallel
```

Expected: `Build-runtime/Release/open-ephys.exe` and nine bundled plugin DLLs.

- [ ] **Step 3: Verify the current fork is IDLE and close it normally**

Read authenticated status for the currently running isolated fork. Proceed
only if it reports `IDLE`. Request a normal application close and verify the
PID and listeners 38498/37497 are gone. Never terminate a process that is
ACQUIRE, RECORD, UNKNOWN, or belongs to the official installation.

- [ ] **Step 4: Launch the new build with a fresh token and state directory**

Use a new at-least-384-bit token held only in the launch environment and the
isolated launcher with `-MaintenanceWindowApproved`. Verify:

```text
one fork PID
--state-dir present
--no-http present
--no-user-plugins present
--agent-uia-readonly present
127.0.0.1:38498 owned by the new PID
no 37497 listener owned by the new PID
mode IDLE
mutation_allowed false
nine bundled plugins loaded without failure
```

- [ ] **Step 5: Run the real Windows UIA verifier**

Run `Test-AgentAccessibility.ps1` in the token-bearing launch environment.
Expected: pass with exact IDs, two children, read-only ValuePattern, no Invoke
or Toggle patterns, and unchanged native mode/revision after 10,000 reads.

- [ ] **Step 6: Verify default-disabled behavior in a separate safe launch**

After normally closing the accepted instance while IDLE, launch the same build
without `--agent-uia-readonly` in a fresh isolated state directory and without
hardware. Verify the three Agent AutomationIds are absent. Close normally
while IDLE.

- [ ] **Step 7: Save bounded evidence and update the contract**

Write a compact JSON evidence record containing commit, executable hash,
runtime arguments, PID, listener checks, exact UIA results, test counts, and
timestamps. Do not write the token. Update the known-gap section to state that
MSVC build and UIA read-only discovery are complete while UIA mutation remains
intentionally absent.

- [ ] **Step 8: Commit documentation and evidence**

```powershell
git add docs/agent/transport-control-contract-v0.0.1.md docs/agent/evidence/uia-readonly-live-2026-07-16.json
git commit -m "docs: record read-only UIA acceptance"
```

- [ ] **Step 9: Build and verify the runtime package**

Package only from a clean `BUILD_TESTS=OFF` Release directory. Recompute every
manifest SHA-256, require zero mismatches, require no
`gui_testable_source.*`, and require `source_dirty=false`.

- [ ] **Step 10: Run final verification and push**

```powershell
pwsh -NoProfile -File tools\windows\Invoke-AgentChecks.ps1
git diff --check
git status --short
git push
git rev-parse HEAD
git rev-parse '@{u}'
```

Expected: complete test pass, clean worktree, and identical local/upstream
commit IDs.
