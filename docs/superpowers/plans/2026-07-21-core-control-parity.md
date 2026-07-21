# Core Control UIA/API Parity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Open Ephys global acquisition, recording, health, timing, and recording-option controls discoverable and equivalent through GUI, Windows UIA, and the built-in HTTP API.

**Architecture:** Add a transport-neutral capability descriptor as the single identity and semantics source. UI components consume descriptor metadata, while the existing HTTP server serializes the same descriptors and delegates operations to existing `CoreServices` and `ControlPanel` behavior. Runtime contract tests join UIA and API records by canonical `oe.*` ID.

**Tech Stack:** C++17, JUCE 8 accessibility interfaces, cpp-httplib, nlohmann/json, GoogleTest 1.12.1, CMake/Ninja, MSVC 19.40, Windows UI Automation PowerShell APIs.

## Global Constraints

- Baseline remains official Open Ephys GUI 1.0.2 commit `c91afebcfb0678a667fb93f6312ed33c56ec640f`.
- This branch exposes existing behavior only; it adds no acquisition, recording, analysis, visualization, or instrument feature.
- The canonical capability ID is also the Windows AutomationId and API discovery ID.
- Existing `/api/*` routes and response shapes remain compatible.
- No external agent server and no coordinate-based automation are introduced.
- GUI, UIA, and API callers use the same validation, acquisition locks, state transitions, and error meaning.
- Each task follows RED → GREEN → full regression verification → one focused commit.
- Build with `-DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DOE_DONT_CHECK_BUILD_PATH=TRUE`.

---

## File map

| File | Responsibility |
|---|---|
| `Source/Utils/ControlCapability.h` | Transport-neutral descriptor and operation types |
| `Source/Utils/ControlCapability.cpp` | Validation, static core descriptors, lookup |
| `Source/Utils/ControlCapabilityJson.h/.cpp` | Manifest serialization for the built-in HTTP API |
| `Source/Utils/ControlStatus.h` | Transport-neutral clock and recording-option state snapshots |
| `Source/Utils/OpenEphysHttpServer.h` | Existing HTTP transport and new discovery/status routes |
| `Source/CoreServices.h/.cpp` | Shared state access used by GUI and API |
| `Source/UI/ControlPanel.h/.cpp` | Existing GUI behavior and UIA adapter metadata/state |
| `Tests/API/ControlCapabilityTests.cpp` | Descriptor, JSON, and parity contract unit tests |
| `Tests/UI/ControlPanelAccessibilityTests.cpp` | JUCE accessibility role/value/action tests |
| `Resources/Scripts/verify_open_ephys_control_parity.ps1` | Live Windows UIA/API join and equality gate |

---

### Task 1: Canonical capability descriptor and API test target

**Files:**
- Create: `Source/Utils/ControlCapability.h`
- Create: `Source/Utils/ControlCapability.cpp`
- Modify: `Source/Utils/CMakeLists.txt`
- Create: `Tests/API/CMakeLists.txt`
- Create: `Tests/API/ControlCapabilityTests.cpp`
- Modify: `Tests/CMakeLists.txt`

**Interfaces:**
- Consumes: JUCE `String`, `StringArray`, and the existing `TESTABLE` export macro.
- Produces: `ControlCapability`, `ControlApiOperation`, `getCoreControlCapabilities()`, `findControlCapability(StringRef)`.

- [ ] **Step 1: Write the failing descriptor test**

Add `Tests/API/CMakeLists.txt`:

```cmake
include(../ComponentRules.cmake)
add_sources(${COMPONENT_NAME}_tests
        ControlCapabilityTests.cpp
)
target_include_directories(${COMPONENT_NAME}_tests PRIVATE "${SOURCE_DIRECTORY}")
```

Add `add_subdirectory(API)` to `Tests/CMakeLists.txt`.

Add `Tests/API/ControlCapabilityTests.cpp`:

```cpp
#include "../../Source/Utils/ControlCapability.h"
#include "gtest/gtest.h"

TEST (ControlCapabilityTests, DefinesStableCoreControlContracts)
{
    const auto capabilities = getCoreControlCapabilities();
    const StringArray expectedIds {
        "oe.control.acquisition",
        "oe.control.recording",
        "oe.control.recording.options",
        "oe.control.recording.filename",
        "oe.control.recording.new_directory",
        "oe.control.recording.force_new_directory",
        "oe.status.cpu_usage",
        "oe.status.disk_usage",
        "oe.status.elapsed_time"
    };

    EXPECT_EQ (capabilities.size(), expectedIds.size());

    for (const auto& id : expectedIds)
    {
        const auto* capability = findControlCapability (id);
        ASSERT_NE (capability, nullptr) << id;
        EXPECT_EQ (capability->id, id);
        EXPECT_EQ (capability->uiaAutomationId, id);
        EXPECT_TRUE (capability->name.isNotEmpty());
        EXPECT_TRUE (capability->description.isNotEmpty());
        EXPECT_FALSE (capability->operations.empty());
    }
}
```

- [ ] **Step 2: Run the test and verify RED**

Run:

```powershell
cmake -S . -B Build-Tests -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DOE_DONT_CHECK_BUILD_PATH=TRUE
cmake --build Build-Tests --target API_tests -j 4
```

Expected: compile failure because `ControlCapability.h` and its functions do not exist.

- [ ] **Step 3: Implement the descriptor model**

Create `Source/Utils/ControlCapability.h`:

```cpp
#ifndef CONTROL_CAPABILITY_H
#define CONTROL_CAPABILITY_H

#include "../../JuceLibraryCode/JuceHeader.h"
#include "../TestableExport.h"

enum class ControlCapabilityKind
{
    action,
    toggle,
    value,
    range,
    status,
    selection,
    collection
};

struct ControlApiOperation
{
    String operation;
    String method;
    String path;
    StringArray requestFields;
    StringArray responseFields;
};

struct ControlCapability
{
    String id;
    String name;
    String description;
    ControlCapabilityKind kind;
    String uiaAutomationId;
    std::vector<ControlApiOperation> operations;
};

TESTABLE const std::vector<ControlCapability>& getCoreControlCapabilities();
TESTABLE const ControlCapability* findControlCapability (StringRef id);

#endif
```

Create `Source/Utils/ControlCapability.cpp` with one descriptor per expected ID. Use existing routes for acquisition, recording, filename, new directory, and CPU. Use the new routes reserved by this plan for force-new-directory, disk usage, and elapsed time:

```cpp
#include "ControlCapability.h"

const std::vector<ControlCapability>& getCoreControlCapabilities()
{
    static const std::vector<ControlCapability> capabilities {
        { "oe.control.acquisition", "Acquisition", "Start or stop data acquisition.",
          ControlCapabilityKind::toggle, "oe.control.acquisition",
          { { "read", "GET", "/api/status", {}, { "mode" } },
            { "set", "PUT", "/api/status", { "mode" }, { "mode" } } } },
        { "oe.control.recording", "Recording", "Start or stop writing data to disk.",
          ControlCapabilityKind::toggle, "oe.control.recording",
          { { "read", "GET", "/api/status", {}, { "mode" } },
            { "set", "PUT", "/api/status", { "mode" }, { "mode" } } } },
        { "oe.control.recording.options", "Recording options", "Show or hide recording options.",
          ControlCapabilityKind::toggle, "oe.control.recording.options",
          { { "read", "GET", "/api/recording/options", {}, { "expanded" } },
            { "set", "PUT", "/api/recording/options", { "expanded" }, { "expanded" } } } },
        { "oe.control.recording.filename", "Recording filename", "Edit the recording filename.",
          ControlCapabilityKind::collection, "oe.control.recording.filename",
          { { "read", "GET", "/api/recording", {}, { "prepend_text", "base_text", "append_text" } },
            { "set", "PUT", "/api/recording", { "prepend_text", "base_text", "append_text" },
              { "prepend_text", "base_text", "append_text" } } } },
        { "oe.control.recording.new_directory", "New recording directory", "Start a new data directory for the next recording.",
          ControlCapabilityKind::toggle, "oe.control.recording.new_directory",
          { { "read", "GET", "/api/recording/options", {}, { "new_directory_requested" } },
            { "set", "PUT", "/api/recording/options", { "new_directory_requested" }, { "new_directory_requested" } } } },
        { "oe.control.recording.force_new_directory", "Force new recording directories", "Force a new data directory for each recording.",
          ControlCapabilityKind::toggle, "oe.control.recording.force_new_directory",
          { { "read", "GET", "/api/recording/options", {}, { "force_new_directory" } },
            { "set", "PUT", "/api/recording/options", { "force_new_directory" }, { "force_new_directory" } } } },
        { "oe.status.cpu_usage", "CPU usage", "Fraction of available processing time used by the signal chain.",
          ControlCapabilityKind::range, "oe.status.cpu_usage",
          { { "read", "GET", "/api/cpu", {}, { "usage" } } } },
        { "oe.status.disk_usage", "Disk usage", "Fraction of recording-volume space currently used.",
          ControlCapabilityKind::range, "oe.status.disk_usage",
          { { "read", "GET", "/api/disk", {}, { "usage" } } } },
        { "oe.status.elapsed_time", "Elapsed time", "Elapsed acquisition or recording time.",
          ControlCapabilityKind::status, "oe.status.elapsed_time",
          { { "read", "GET", "/api/time", {}, { "display" } } } }
    };
    return capabilities;
}

const ControlCapability* findControlCapability (StringRef id)
{
    const String target (id);
    const auto& capabilities = getCoreControlCapabilities();
    const auto result = std::find_if (capabilities.begin(), capabilities.end(),
                                      [&] (const auto& capability) { return capability.id == target; });
    return result == capabilities.end() ? nullptr : &*result;
}
```

Add both source files to `Source/Utils/CMakeLists.txt`.

- [ ] **Step 4: Run descriptor tests and the existing UI suite**

Run:

```powershell
cmake --build Build-Tests --target API_tests UI_tests -j 4
cmake -E copy_directory .\Build-Tests\TestBin\common .\Build-Tests\TestBin\API
.\Build-Tests\TestBin\API\API_tests.exe
cmake -E copy_directory .\Build-Tests\TestBin\common .\Build-Tests\TestBin\UI
.\Build-Tests\TestBin\UI\UI_tests.exe
```

Expected: all API and UI tests pass.

- [ ] **Step 5: Commit**

```powershell
git add Source/Utils/ControlCapability.h Source/Utils/ControlCapability.cpp Source/Utils/CMakeLists.txt Tests/API/CMakeLists.txt Tests/API/ControlCapabilityTests.cpp Tests/CMakeLists.txt
git commit -m "feat(api): define canonical core capabilities"
```

---

### Task 2: Capability JSON and `/api/capabilities`

**Files:**
- Create: `Source/Utils/ControlCapabilityJson.h`
- Create: `Source/Utils/ControlCapabilityJson.cpp`
- Modify: `Source/Utils/CMakeLists.txt`
- Modify: `Source/Utils/OpenEphysHttpServer.h`
- Modify: `Tests/API/ControlCapabilityTests.cpp`

**Interfaces:**
- Consumes: `getCoreControlCapabilities()` from Task 1.
- Produces: `nlohmann::json controlCapabilitiesToJson(const std::vector<ControlCapability>&)` and `GET /api/capabilities`.

- [ ] **Step 1: Write the failing JSON contract test**

Append:

```cpp
#include "../../Source/Utils/ControlCapabilityJson.h"

TEST (ControlCapabilityTests, SerialisesApiAndUiaMetadataByCanonicalId)
{
    const auto document = controlCapabilitiesToJson (getCoreControlCapabilities());
    ASSERT_TRUE (document["capabilities"].is_array());
    ASSERT_EQ (document["capabilities"].size(), 8);

    const auto acquisition = std::find_if (document["capabilities"].begin(),
                                           document["capabilities"].end(),
                                           [] (const auto& item)
                                           { return item["id"] == "oe.control.acquisition"; });
    ASSERT_NE (acquisition, document["capabilities"].end());
    EXPECT_EQ ((*acquisition)["uia"]["automation_id"], "oe.control.acquisition");
    EXPECT_EQ ((*acquisition)["api"][0]["path"], "/api/status");
    EXPECT_EQ ((*acquisition)["api"][0]["method"], "GET");
}
```

- [ ] **Step 2: Run and verify RED**

Run `cmake --build Build-Tests --target API_tests -j 4`.

Expected: compile failure because `ControlCapabilityJson.h` is missing.

- [ ] **Step 3: Implement deterministic serialization**

Create `ControlCapabilityJson.h`:

```cpp
#ifndef CONTROL_CAPABILITY_JSON_H
#define CONTROL_CAPABILITY_JSON_H

#include "ControlCapability.h"
#include "json.hpp"

TESTABLE nlohmann::json controlCapabilitiesToJson (const std::vector<ControlCapability>& capabilities);

#endif
```

Create `ControlCapabilityJson.cpp`:

```cpp
#include "ControlCapabilityJson.h"

namespace
{
const char* kindToString (ControlCapabilityKind kind)
{
    switch (kind)
    {
        case ControlCapabilityKind::action: return "action";
        case ControlCapabilityKind::toggle: return "toggle";
        case ControlCapabilityKind::value: return "value";
        case ControlCapabilityKind::range: return "range";
        case ControlCapabilityKind::status: return "status";
        case ControlCapabilityKind::selection: return "selection";
        case ControlCapabilityKind::collection: return "collection";
    }

    jassertfalse;
    return "status";
}

nlohmann::json stringArrayToJson (const StringArray& values)
{
    auto result = nlohmann::json::array();
    for (const auto& value : values)
        result.push_back (value.toStdString());
    return result;
}
}

nlohmann::json controlCapabilitiesToJson (const std::vector<ControlCapability>& capabilities)
{
    nlohmann::json result;
    result["capabilities"] = nlohmann::json::array();

    for (const auto& capability : capabilities)
    {
        nlohmann::json item;
        item["id"] = capability.id.toStdString();
        item["name"] = capability.name.toStdString();
        item["description"] = capability.description.toStdString();
        item["kind"] = kindToString (capability.kind);
        item["uia"]["automation_id"] = capability.uiaAutomationId.toStdString();
        item["api"] = nlohmann::json::array();

        for (const auto& operation : capability.operations)
        {
            item["api"].push_back ({
                { "operation", operation.operation.toStdString() },
                { "method", operation.method.toStdString() },
                { "path", operation.path.toStdString() },
                { "request_fields", stringArrayToJson (operation.requestFields) },
                { "response_fields", stringArrayToJson (operation.responseFields) }
            });
        }

        result["capabilities"].push_back (std::move (item));
    }

    return result;
}
```

Register this route at the beginning of `OpenEphysHttpServer::run()`:

```cpp
svr_->Get ("/api/capabilities", [] (const httplib::Request&, httplib::Response& res)
{
    const auto document = controlCapabilitiesToJson (getCoreControlCapabilities());
    res.set_content (document.dump(), "application/json");
});
```

- [ ] **Step 4: Run API/UI tests and build the application**

Run:

```powershell
cmake --build Build-Tests --target API_tests UI_tests open-ephys -j 4
.\Build-Tests\TestBin\API\API_tests.exe
.\Build-Tests\TestBin\UI\UI_tests.exe
```

Expected: all tests pass and `open-ephys.exe` links.

- [ ] **Step 5: Commit**

```powershell
git add Source/Utils/ControlCapabilityJson.h Source/Utils/ControlCapabilityJson.cpp Source/Utils/CMakeLists.txt Source/Utils/OpenEphysHttpServer.h Tests/API/ControlCapabilityTests.cpp
git commit -m "feat(api): publish capability discovery"
```

---

### Task 3: Shared disk-usage state and `/api/disk`

**Files:**
- Modify: `Source/CoreServices.h`
- Modify: `Source/CoreServices.cpp`
- Modify: `Source/UI/ControlPanel.cpp`
- Modify: `Source/Utils/OpenEphysHttpServer.h`
- Modify: `Tests/API/ControlCapabilityTests.cpp`

**Interfaces:**
- Produces: `float CoreServices::calculateDiskUsage(int64 bytesFree, int64 totalBytes)` and `float CoreServices::getRecordingDiskUsage()`.
- Consumes: the same `getRecordingDiskUsage()` from both GUI refresh and API response.

- [ ] **Step 1: Write failing calculation tests**

```cpp
TEST (ControlCapabilityTests, CalculatesBoundedDiskUsage)
{
    EXPECT_FLOAT_EQ (CoreServices::calculateDiskUsage (25, 100), 0.75f);
    EXPECT_FLOAT_EQ (CoreServices::calculateDiskUsage (100, 100), 0.0f);
    EXPECT_FLOAT_EQ (CoreServices::calculateDiskUsage (0, 100), 1.0f);
    EXPECT_FLOAT_EQ (CoreServices::calculateDiskUsage (1, 0), 0.0f);
    EXPECT_FLOAT_EQ (CoreServices::calculateDiskUsage (120, 100), 0.0f);
}
```

- [ ] **Step 2: Run and verify RED**

Run:

```powershell
cmake --build Build-Tests --target API_tests -j 4
```

Expected: compile failure because `calculateDiskUsage` is absent.

- [ ] **Step 3: Implement one shared calculation**

```cpp
float CoreServices::calculateDiskUsage (int64 bytesFree, int64 totalBytes)
{
    if (totalBytes <= 0)
        return 0.0f;

    return jlimit (0.0f, 1.0f,
                   1.0f - static_cast<float> (bytesFree) / static_cast<float> (totalBytes));
}

float CoreServices::getRecordingDiskUsage()
{
    const auto directory = getRecordingParentDirectory();
    return calculateDiskUsage (directory.getBytesFreeOnVolume(), directory.getVolumeTotalSize());
}
```

Replace the formula in `ControlPanel::refreshMeters()` with:

```cpp
diskMeter->updateDiskSpace (CoreServices::getRecordingDiskUsage());
```

Add the route:

```cpp
svr_->Get ("/api/disk", [] (const httplib::Request&, httplib::Response& res)
{
    json ret;
    ret["capability"] = "oe.status.disk_usage";
    ret["usage"] = CoreServices::getRecordingDiskUsage();
    ret["minimum"] = 0.0;
    ret["maximum"] = 1.0;
    ret["read_only"] = true;
    res.set_content (ret.dump(), "application/json");
});
```

- [ ] **Step 4: Run API/UI tests and build**

Run:

```powershell
cmake --build Build-Tests --target API_tests UI_tests open-ephys -j 4
cmake -E copy_directory .\Build-Tests\TestBin\common .\Build-Tests\TestBin\API
.\Build-Tests\TestBin\API\API_tests.exe
cmake -E copy_directory .\Build-Tests\TestBin\common .\Build-Tests\TestBin\UI
.\Build-Tests\TestBin\UI\UI_tests.exe
```

Expected: calculation edge cases, all UI tests, and Release link pass.

- [ ] **Step 5: Commit**

```powershell
git add Source/CoreServices.h Source/CoreServices.cpp Source/UI/ControlPanel.cpp Source/Utils/OpenEphysHttpServer.h Tests/API/ControlCapabilityTests.cpp
git commit -m "feat(api): expose shared disk usage"
```

---

### Task 4: Shared elapsed-time state and `/api/time`

**Files:**
- Create: `Source/Utils/ControlStatus.h`
- Modify: `Source/Utils/CMakeLists.txt`
- Modify: `Source/UI/ControlPanel.h`
- Modify: `Source/UI/ControlPanel.cpp`
- Modify: `Source/CoreServices.h`
- Modify: `Source/CoreServices.cpp`
- Modify: `Source/Utils/OpenEphysHttpServer.h`
- Modify: `Tests/UI/ControlPanelAccessibilityTests.cpp`
- Modify: `Tests/API/ControlCapabilityTests.cpp`

**Interfaces:**
- Produces: `ClockStatus Clock::getStatus() const`, `ClockStatus CoreServices::getClockStatus()`.
- `ClockStatus` is declared in `Source/Utils/ControlStatus.h`; its fields are `display`, `elapsedMilliseconds`, `mode`, `reference`, `running`, `recording`.

- [ ] **Step 1: Write failing state-equivalence tests**

Use a newly constructed `TestClock`, whose zero state is deterministic:

```cpp
TEST (ControlPanelAccessibilityTests, UsesClockStatusForItsAccessibleValue)
{
    TestClock clock;
    const auto state = clock.getStatus();
    auto handler = clock.createAccessibilityHandler();
    ASSERT_NE (handler->getValueInterface(), nullptr);
    EXPECT_EQ (state.display, handler->getValueInterface()->getCurrentValueAsString());
    EXPECT_EQ (state.elapsedMilliseconds, 0);
    EXPECT_FALSE (state.running);
    EXPECT_FALSE (state.recording);
}
```

- [ ] **Step 2: Run and verify RED**

Run:

```powershell
cmake --build Build-Tests --target UI_tests -j 4
```

Expected: compile failure because `ClockStatus` and `getStatus()` do not exist.

- [ ] **Step 3: Implement and route the shared state**

Define:

```cpp
struct ClockStatus
{
    String display;
    int64 elapsedMilliseconds;
    String mode;
    String reference;
    bool running;
    bool recording;
};
```

Place this structure in `Source/Utils/ControlStatus.h`, include JUCE's `String` definition there, and list the header in `Source/Utils/CMakeLists.txt`. Store stable uppercase enum names (`DEFAULT` or `HHMMSS`, and `CUMULATIVE` or `ACQUISITION_START`) in `mode` and `reference`, so the transport-neutral type does not depend on nested `Clock` enums. Make `Clock::getStatus()` select the same elapsed counter used by `getDisplayText()`. Change the UIA text-value lambda to `return getStatus().display;`. Add `ControlPanel::getClockStatus()` and `CoreServices::getClockStatus()` pass-through methods.

Add `GET /api/time` returning:

```json
{
  "capability": "oe.status.elapsed_time",
  "display": "0 min 0 s",
  "elapsed_milliseconds": 0,
  "mode": "DEFAULT",
  "reference": "CUMULATIVE",
  "running": false,
  "recording": false,
  "read_only": true
}
```

- [ ] **Step 4: Verify UI and API tests plus Release build**

Run:

```powershell
cmake --build Build-Tests --target API_tests UI_tests open-ephys -j 4
cmake -E copy_directory .\Build-Tests\TestBin\common .\Build-Tests\TestBin\API
.\Build-Tests\TestBin\API\API_tests.exe
cmake -E copy_directory .\Build-Tests\TestBin\common .\Build-Tests\TestBin\UI
.\Build-Tests\TestBin\UI\UI_tests.exe
```

Expected: the state string equals UIA value and JSON serializer output in both clock modes.

- [ ] **Step 5: Commit**

```powershell
git add Source/Utils/ControlStatus.h Source/Utils/CMakeLists.txt Source/UI/ControlPanel.h Source/UI/ControlPanel.cpp Source/CoreServices.h Source/CoreServices.cpp Source/Utils/OpenEphysHttpServer.h Tests/UI/ControlPanelAccessibilityTests.cpp Tests/API/ControlCapabilityTests.cpp
git commit -m "feat(api): expose shared elapsed time"
```

---

### Task 5: Recording-option shared operations and parity

**Files:**
- Modify: `Source/Utils/ControlStatus.h`
- Modify: `Source/UI/ControlPanel.h`
- Modify: `Source/UI/ControlPanel.cpp`
- Modify: `Source/CoreServices.h`
- Modify: `Source/CoreServices.cpp`
- Modify: `Source/Utils/OpenEphysHttpServer.h`
- Modify: `Tests/UI/ControlPanelAccessibilityTests.cpp`
- Modify: `Tests/API/ControlCapabilityTests.cpp`

**Interfaces:**
- Produces: `RecordingOptionsStatus` in `ControlStatus.h`, `ControlPanel::getRecordingOptionsStatus()`, `ControlPanel::setRecordingOptionsExpanded(bool)`, `ControlPanel::setNewDirectoryRequested(bool)`, `ControlPanel::setForceNewDirectory(bool)`, and CoreServices pass-throughs.
- Keeps button callbacks as adapters over these methods.

- [ ] **Step 1: Write failing state-transition tests**

Construct `ControlPanel(nullptr, nullptr, true)` and verify console-safe recording options:

```cpp
TEST (ControlPanelAccessibilityTests, SharesForceNewDirectoryStateWithTheButton)
{
    ControlPanel panel (nullptr, nullptr, true);
    panel.setForceNewDirectory (true);
    EXPECT_TRUE (panel.getRecordingOptionsStatus().forceNewDirectory);
    EXPECT_TRUE (panel.getRecordingOptionsStatus().newDirectoryRequested);

    panel.setForceNewDirectory (false);
    EXPECT_FALSE (panel.getRecordingOptionsStatus().forceNewDirectory);

    panel.setNewDirectoryRequested (false);
    EXPECT_FALSE (panel.getRecordingOptionsStatus().newDirectoryRequested);

    panel.setRecordingOptionsExpanded (true);
    EXPECT_TRUE (panel.getRecordingOptionsStatus().expanded);
}
```

- [ ] **Step 2: Run and verify RED**

Run:

```powershell
cmake --build Build-Tests --target UI_tests -j 4
```

Expected: compile failure because the shared state and setter are absent.

- [ ] **Step 3: Extract existing button behavior**

Add this transport-neutral snapshot to `Source/Utils/ControlStatus.h`:

```cpp
struct RecordingOptionsStatus
{
    bool expanded;
    bool forceNewDirectory;
    bool newDirectoryRequested;
    bool recording;
};
```

Apply `oe.control.recording.options` metadata to `showHideRecordingOptionsButton` and expose its toggle state through UIA. Move the current expansion branch into `setRecordingOptionsExpanded(bool)`, make the `newDirectoryButton` branch call `setNewDirectoryRequested(button->getToggleState())`, and move the current `forceNewDirectoryButton` branch into `setForceNewDirectory(bool)`. The button branches become adapters over those methods; for example:

```cpp
if (button == forceNewDirectoryButton.get())
{
    setForceNewDirectory (button->getToggleState());
    return;
}
```

Add `GET/PUT /api/recording/options`. The PUT handler must dispatch `CoreServices::setForceNewDirectory(value)` to the message thread and return the resulting state. Invalid non-Boolean input returns HTTP 400. If the matching GUI control is disabled in the current application state, return HTTP 409 with `operation_not_available`; do not invent an API-only restriction.

The same PUT route accepts `expanded`, `new_directory_requested`, and `force_new_directory`, delegating to the matching `CoreServices` method. It returns the complete `RecordingOptionsStatus`; omitted fields remain unchanged. Add one table-driven API test whose cases contain each field name, Boolean input, and expected status member; add separate invalid-type and unknown-field cases so API behavior cannot silently diverge from the GUI.

- [ ] **Step 4: Verify state, errors, UI tests, and build**

Run:

```powershell
cmake --build Build-Tests --target API_tests UI_tests open-ephys -j 4
cmake -E copy_directory .\Build-Tests\TestBin\common .\Build-Tests\TestBin\API
.\Build-Tests\TestBin\API\API_tests.exe
cmake -E copy_directory .\Build-Tests\TestBin\common .\Build-Tests\TestBin\UI
.\Build-Tests\TestBin\UI\UI_tests.exe
```

Expected: GUI callback and API operation use the same method; UIA toggle state and returned API Boolean agree.

- [ ] **Step 5: Commit**

```powershell
git add Source/Utils/ControlStatus.h Source/UI/ControlPanel.h Source/UI/ControlPanel.cpp Source/CoreServices.h Source/CoreServices.cpp Source/Utils/OpenEphysHttpServer.h Tests/UI/ControlPanelAccessibilityTests.cpp Tests/API/ControlCapabilityTests.cpp
git commit -m "feat(api): unify recording option controls"
```

---

### Task 6: Live Windows UIA/API parity gate

**Files:**
- Modify: `Resources/Scripts/inspect_open_ephys_uia.ps1`
- Create: `Resources/Scripts/verify_open_ephys_control_parity.ps1`

**Interfaces:**
- Consumes: running source-built Open Ephys, `GET /api/capabilities`, UIA AutomationIds.
- Produces: nonzero exit on missing IDs, duplicate IDs, missing patterns/routes, or unequal state.

- [ ] **Step 1: Write the failing verifier**

The script must require these IDs:

```powershell
$requiredIds = @(
    'oe.control.acquisition',
    'oe.control.recording',
    'oe.control.recording.options',
    'oe.control.recording.filename',
    'oe.control.recording.new_directory',
    'oe.control.recording.force_new_directory',
    'oe.status.cpu_usage',
    'oe.status.disk_usage',
    'oe.status.elapsed_time'
)
```

For each ID, join exactly one UIA record with exactly one capability record. Compare name, AutomationId, read-only state, range, and current value where applicable. Require Toggle for acquisition/recording/force-new-directory, RangeValue for CPU/disk, and Value for elapsed time.

- [ ] **Step 2: Run before all recording options are visible**

Run:

```powershell
$sourceBuild = @(Get-Process open-ephys | Where-Object {
    $_.Path -eq 'C:\Users\wangc\Documents\Cong\01-open-ephys-uia-root\Build-Tests\open-ephys.exe'
})
if ($sourceBuild.Count -ne 1) { throw "Expected one source build, found $($sourceBuild.Count)" }
.\Resources\Scripts\verify_open_ephys_control_parity.ps1 -TargetProcessId $sourceBuild[0].Id -ApiBaseUrl http://127.0.0.1:37497
```

Expected: FAIL listing hidden/unregistered recording-option IDs or missing API state.

- [ ] **Step 3: Add deterministic panel expansion to the verifier**

Use UIA ExpandCollapse or Invoke on `oe.control.recording.options` after taking a before-state snapshot. Never click by coordinates. Wait until the three option controls appear, then query both surfaces. Restore the original expansion state at the end.

- [ ] **Step 4: Run full fresh verification**

Run:

```powershell
cmake --build Build-Tests --target API_tests UI_tests open-ephys -j 4
.\Build-Tests\TestBin\API\API_tests.exe
.\Build-Tests\TestBin\UI\UI_tests.exe
$sourceBuild = @(Get-Process open-ephys | Where-Object {
    $_.Path -eq 'C:\Users\wangc\Documents\Cong\01-open-ephys-uia-root\Build-Tests\open-ephys.exe'
})
if ($sourceBuild.Count -ne 1) { throw "Expected one source build, found $($sourceBuild.Count)" }
.\Resources\Scripts\verify_open_ephys_control_parity.ps1 -TargetProcessId $sourceBuild[0].Id -ApiBaseUrl http://127.0.0.1:37497
git diff --check
```

Expected: all tests exit 0; nine IDs join one-to-one; every required pattern and route exists; CPU, disk, clock, acquisition, recording, and recording-option state agree.

- [ ] **Step 5: Commit**

```powershell
git add Resources/Scripts/inspect_open_ephys_uia.ps1 Resources/Scripts/verify_open_ephys_control_parity.ps1
git commit -m "test(agent): verify core UIA API parity"
```

---

## Follow-on plans

After this plan is green, create separate implementation plans for:

1. Menus, application commands, popup menus, and dialogs.
2. Processor catalog and signal-chain controls.
3. Runtime processor, stream, and parameter capability discovery.
4. Utility panels, tabs, visualizers, and secondary windows.
5. Core processor custom controls.
6. Bundled plugin custom controls and dynamic channels/electrodes.
7. Full inventory conformance and official-development contribution preparation.

Each follow-on plan consumes `ControlCapability`, `/api/capabilities`, and the Windows parity verifier defined here.
