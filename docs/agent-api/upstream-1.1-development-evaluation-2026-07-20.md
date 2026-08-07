# Official 1.1 Development Baseline Evaluation

**Evaluated:** 2026-07-20

**Repository:** `open-ephys/plugin-GUI`

**Pinned upstream commit:** `e4ffd12361915a03679398b4228f3f6d34df5950`

**Declared version:** Open Ephys GUI `1.1.0`, Plugin API `10`

## Decision

The current official `development` tip is technically viable as the source base for a separate Agent Native development line. It is not yet a release-grade baseline and must not be represented as the official Open Ephys 1.1 release.

Create the new line from the exact pinned commit, record the upstream identity as `1.1.0-dev.e4ffd123`, and use an independent product/release identity such as `Open Ephys Agent Native 0.1.0` and tag `agent-native-v0.1.0`. Keep upstream and fork version fields separately visible in builds, diagnostics, and support reports.

Before agent-facing UI or API exposure begins, the clean-build dependency defect and test-environment isolation defect described below should be repaired in small, reviewable PRs. Then the complete clean build and test matrix must pass again.

## Evaluation environment

The evaluation used an isolated detached worktree:

`C:\Users\wangc\Documents\Cong\01-open-ephys-1.1-eval`

Toolchain:

- Visual Studio 2022 Build Tools 17.10.5
- MSVC 19.40.33813 / toolset 14.40.33807
- CMake 4.4.0
- Ninja 1.13.2
- PowerShell 7.6.3
- Python 3.12.2
- VB-Audio Virtual Cable for RecordNode tests

## Build evidence

Configuration succeeded with the Ninja generator in Release mode.

A fresh all-target build failed before compilation because bundled plugin targets required the file `open-ephys.lib`, but Ninja had no target rule that produced it in that dependency path. Building the core target explicitly and then building all targets succeeded:

```powershell
cmake -S . -B Build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build Build --target open-ephys --parallel 4
cmake --build Build --parallel 4
```

Verified outputs:

- `Build\open-ephys.exe`: FileVersion `1.1.0`, ProductVersion `1.1.0`
- nine bundled plugin DLLs: ArduinoOutput, BandpassFilter, ChannelMap, CommonAvgRef, LfpViewer, PhaseDetector, RecordControl, SpikeDetector, and SpikeViewer

This proves the official development source and bundled plugins compile on this PC. It also exposes an ordering/dependency defect in a clean Ninja all-target build. Official Visual Studio-generator CI does not exercise that path.

## Test evidence

The test-enabled build completed successfully:

```powershell
cmake -S . -B Build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build Build --parallel 4
ctest --test-dir Build --output-on-failure
```

CTest discovered 12 executables. Eleven passed. `Processors_tests` reported four failures from 139 tests; all four were in `PluginManagerTest`:

- `PluginLoading`
- `PluginCreation`
- `getLibraryIndexFromPlugin`
- `removePlugin`

The tests looked for ArduinoOutput under `Build\ArduinoOutput`, while the generated DLL was at `Build\plugins\ArduinoOutput.dll`. Rerunning Processors tests with only `PluginManagerTest.*` excluded produced 135 passes from 24 suites.

The passing coverage includes:

- all nine RecordNode tests, including the two synthetic hardware-synchronized per-sample timestamp cases
- SequentialBlockFile, multi-stream, buffer-resize, and high-channel-count tests
- all 31 SIMDConverter tests
- ProcessorGraph XML loading
- MessageCenter, GenericProcessor, DataThread, event, metadata, and parameter tests
- all other CTest executables, including UI and JUCE tests

Test logs also attempted to load the locally installed `acquisition-board.dll` and reported Windows error 126. This did not fail the remaining tests, but it proves that the suite scans the user's installed plugin environment and is therefore not hermetic.

## What is not yet proven

This evaluation does not establish:

- end-to-end GUI startup and interaction behavior on a clean Windows profile
- acquisition, synchronization, and recording with real laboratory hardware
- compatibility with every external Plugin API 10 plugin
- installer, signing, ZIP contents, upgrade, or rollback behavior
- Agent Native UI Automation labels, stable control identities, or a programmatic command/state API
- official 1.1 release status; upstream `testing` and `main` still point to v1.0.2 at evaluation time

## Required baseline PRs

Keep each correction isolated and independently testable.

1. **Clean build graph:** make every bundled plugin depend on the CMake `open-ephys` target/artifact correctly so a fresh `cmake --build Build` succeeds without a manual core-first step. Validate Ninja and the official Visual Studio generator. This is a good upstream contribution candidate.
2. **Plugin test path:** pass the generated ArduinoOutput target path to `PluginManagerTest` instead of assuming `Build\ArduinoOutput`. This is a good upstream contribution candidate.
3. **Hermetic plugin tests:** prevent tests from scanning user-installed plugins, or provide an explicit isolated test plugin directory. Assert that unrelated local DLLs cannot affect results.
4. **Windows GUI smoke test:** launch the locally built executable on a clean test configuration and verify startup dialog, processor insertion, signal-chain save/load, acquisition start/stop, and graceful exit.
5. **Hardware and external-plugin matrix:** validate representative acquisition hardware, Record Node output, timestamp/synchronization behavior, and the required external Plugin API 10 set.

Only after PRs 1-3 and the complete clean build/test rerun should the Agent Native interface work begin.

## Agent Native development boundary

The Agent Native line should expose and normalize existing Open Ephys behavior rather than add scientific-processing features. Its first interface work should be split into small PRs:

1. inventory existing GUI actions, state, labels, preconditions, and errors
2. correct Windows UI Automation names, roles, stable automation IDs, enabled state, and value semantics
3. introduce a shared internal command/state model used by both GUI handlers and programmatic adapters
4. expose read-only state and capability discovery first
5. expose bounded commands with validation, typed errors, idempotency rules, and audit records
6. add contract tests that prove GUI and API invoke the same behavior and report the same state

Do not implement a second hidden control path, coordinate-based semantics, or an API that bypasses the GUI's validation and safety rules.

## Version and branch policy

- Upstream source: exact commit `e4ffd123`, never a floating `development` head in a release build.
- Integration branch: `agent-native` or `agent-native/0.1` in the fork.
- Fork version: start at `0.1.0`; do not publish it as Open Ephys `1.1.0`.
- Compatibility metadata: report `upstream_gui=1.1.0-dev`, `upstream_commit=e4ffd123`, and `plugin_api=10` separately.
- Update the upstream pin only through a dedicated synchronization PR with a full diff review and build/test matrix.
- Keep one behavior/fix per PR and one auditable release increment per accepted group.

## Go/no-go result

**Go for a pinned development branch and baseline-hardening work.**

**No-go for calling it a stable baseline, shipping it to the laboratory, or beginning broad Agent Native API/UI exposure until clean-build, test isolation, GUI smoke, and hardware validation gates are satisfied.**
