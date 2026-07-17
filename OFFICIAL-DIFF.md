# Open Ephys GUI v1.0.2 official difference registry

Official baseline: tag `v1.0.2`, commit
`c91afebcfb0678a667fb93f6312ed33c56ec640f`.

Every changed production path is registered explicitly. `Agent mode off` or
omitting Agent launch flags is the runtime rollback unless a row says
otherwise. Scientific acquisition and recording parity remains a release gate.

| Path | Class | Official behavior | Fork behavior | Risk | Verification | Rollback |
|---|---|---|---|---|---|---|
| JuceLibraryCode/modules/juce_gui_basics/native/accessibility/juce_AccessibilityElement_windows.cpp | MODIFIED | JUCE accessibility element behavior | Preserves stable Windows automation IDs | Accessibility-only Windows behavior | UIA verifier and source contract | Build official JUCE baseline |
| Source/AccessClass.cpp | MODIFIED | No Agent endpoint access | Owns Agent endpoint access | Lifecycle coupling | Agent endpoint tests | Agent mode off |
| Source/AccessClass.h | MODIFIED | No Agent endpoint access | Declares Agent endpoint access | Lifecycle coupling | Agent endpoint tests | Agent mode off |
| Source/Agent/AgentAccessibilityBridge.cpp | ADDED | No Agent accessibility bridge | Read-only semantic UIA tree | Additional UI surface | UIA live acceptance | Agent UIA flag off |
| Source/Agent/AgentAccessibilityBridge.h | ADDED | No Agent accessibility bridge | Declares read-only UIA bridge | Additional UI surface | UIA source contract | Agent UIA flag off |
| Source/Agent/AgentAccessibilityState.cpp | ADDED | No Agent accessibility values | Maps observed native state to UIA values | Incorrect mapping | AgentAccessibilityStateTests | Agent UIA flag off |
| Source/Agent/AgentAccessibilityState.h | ADDED | No Agent accessibility values | Declares UIA state mapping | Incorrect mapping | AgentAccessibilityStateTests | Agent UIA flag off |
| Source/Agent/AgentCommand.h | ADDED | No typed Agent commands | Defines typed command origin and kind | Incorrect dispatch | AgentCommandTests | Agent mode off |
| Source/Agent/AgentControlProtocol.cpp | ADDED | No Agent JSON contract | Strict authenticated protocol serialization | Parser exposure | AgentControlProtocolTests | Agent mode off |
| Source/Agent/AgentControlProtocol.h | ADDED | No Agent JSON contract | Declares strict protocol | Parser exposure | AgentControlProtocolTests | Agent mode off |
| Source/Agent/AgentExperimentDirectory.cpp | ADDED | No Agent directory policy | Validates exact independent per-part directory targets | Stricter Agent naming behavior | AgentExperimentDirectoryTests | Agent mode off |
| Source/Agent/AgentExperimentDirectory.h | ADDED | No Agent directory policy | Declares fail-closed directory request and decision types | Stricter Agent naming behavior | AgentExperimentDirectoryTests | Agent mode off |
| Source/Agent/AgentExperimentDirectoryEndpoint.cpp | ADDED | No Agent directory endpoint | Schedules exact-directory preparation on the GUI message thread with bounded idempotency | Directory lifecycle and duplicate request risk | AgentExperimentDirectoryEndpointTests | Agent mode off |
| Source/Agent/AgentExperimentDirectoryEndpoint.h | ADDED | No Agent directory endpoint | Declares asynchronous directory request, result, and snapshot contract | Directory lifecycle coupling | AgentExperimentDirectoryEndpointTests | Agent mode off |
| Source/Agent/AgentLoopbackServer.cpp | ADDED | No Agent listener | Authenticated literal-loopback server | New local control surface | AgentLoopbackServerTests | Agent mode off |
| Source/Agent/AgentLoopbackServer.h | ADDED | No Agent listener | Declares loopback server | New local control surface | AgentLoopbackServerTests | Agent mode off |
| Source/Agent/AgentState.cpp | ADDED | No Agent state revision | Observed state and monotonic revision | Stale state | AgentStateTests | Agent mode off |
| Source/Agent/AgentState.h | ADDED | No Agent state revision | Declares observed state | Stale state | AgentStateTests | Agent mode off |
| Source/Agent/AgentStateSnapshotCache.cpp | ADDED | No Agent snapshot cache | Thread-safe bounded state snapshot | Stale snapshot | AgentStateSnapshotCacheTests | Agent mode off |
| Source/Agent/AgentStateSnapshotCache.h | ADDED | No Agent snapshot cache | Declares snapshot cache | Stale snapshot | AgentStateSnapshotCacheTests | Agent mode off |
| Source/Agent/AgentTransportCoordinator.cpp | ADDED | No Agent transport coordinator | Applies typed state plans with readback | Incorrect transition | AgentTransportCoordinatorTests | Mutation flag off |
| Source/Agent/AgentTransportCoordinator.h | ADDED | No Agent transport coordinator | Declares transport coordinator | Incorrect transition | AgentTransportCoordinatorTests | Mutation flag off |
| Source/Agent/AgentTransportEndpoint.cpp | ADDED | No Agent endpoint | Schedules endpoint work on GUI thread | Thread/lifecycle risk | AgentTransportEndpointTests | Agent mode off |
| Source/Agent/AgentTransportEndpoint.h | ADDED | No Agent endpoint | Declares endpoint lifecycle | Thread/lifecycle risk | AgentTransportEndpointTests | Agent mode off |
| Source/Agent/AgentTransportMailbox.cpp | ADDED | No command mailbox | Idempotent bounded request tracking | Duplicate command risk | AgentTransportMailboxTests | Mutation flag off |
| Source/Agent/AgentTransportMailbox.h | ADDED | No command mailbox | Declares mailbox states | Duplicate command risk | AgentTransportMailboxTests | Mutation flag off |
| Source/Agent/AgentTransportPlanner.cpp | ADDED | No Agent transition planner | Plans explicit acquisition/recording steps | Unsafe plan | AgentTransportPlannerTests | Mutation flag off |
| Source/Agent/AgentTransportPlanner.h | ADDED | No Agent transition planner | Declares planner contract | Unsafe plan | AgentTransportPlannerTests | Mutation flag off |
| Source/Agent/CMakeLists.txt | ADDED | No Agent sources | Builds experimental Agent sources | Build composition | Release build | Build official tag |
| Source/Agent/ControlPanelCommandRouter.cpp | ADDED | No semantic Agent command routing | Routes semantic commands through existing dispatcher | Wrong origin/action | ControlPanelCommandRouterTests | Agent mode off |
| Source/Agent/ControlPanelCommandRouter.h | ADDED | No semantic Agent command routing | Declares semantic router | Wrong origin/action | ControlPanelCommandRouterTests | Agent mode off |
| Source/Agent/ControlPanelExperimentAdapter.cpp | ADDED | No Agent directory adapter | Applies validated directory preparation only through the existing ControlPanel fields on the GUI thread | Recording-name configuration mismatch | ControlPanel adapter source contract and Release build | Agent mode off |
| Source/Agent/ControlPanelExperimentAdapter.h | ADDED | No Agent directory adapter | Declares the bounded ControlPanel directory adapter | UI lifecycle coupling | ControlPanel adapter source contract and Release build | Agent mode off |
| Source/Agent/RuntimeOptions.cpp | ADDED | No Agent flags | Parses fail-closed Agent runtime flags | Isolation bypass | RuntimeOptionsTests | Omit Agent flags |
| Source/Agent/RuntimeOptions.h | ADDED | No Agent flags | Declares runtime options | Isolation bypass | RuntimeOptionsTests | Omit Agent flags |
| Source/Agent/TransportAccessibilityRegistry.cpp | ADDED | No transport UIA registry | Defines read-only semantic transport nodes | Incorrect accessibility metadata | TransportAccessibilityRegistryTests | Agent UIA flag off |
| Source/Agent/TransportAccessibilityRegistry.h | ADDED | No transport UIA registry | Declares UIA registry | Incorrect accessibility metadata | TransportAccessibilityRegistryTests | Agent UIA flag off |
| Source/CMakeLists.txt | MODIFIED | No Agent subdirectory | Includes Agent sources | Build composition | Release and AgentCore builds | Build official tag |
| Source/CoreServices.cpp | MODIFIED | Official CoreServices only | Adds bounded Agent observation hooks | Core-state coupling | AgentState and parity tests | Agent mode off |
| Source/CoreServices.h | MODIFIED | Official CoreServices only | Declares bounded Agent observation hooks | Core-state coupling | AgentState and parity tests | Agent mode off |
| Source/Main.cpp | MODIFIED | Official command line | Applies isolated state directory and Agent flags | Startup behavior | RuntimeOptionsTests and isolated acceptance | Omit Agent flags |
| Source/MainWindow.cpp | MODIFIED | Official window lifecycle | Starts/stops optional Agent endpoint and UIA bridge | Window lifecycle | UIA and loopback live acceptance | Agent mode off |
| Source/MainWindow.h | MODIFIED | Official window lifecycle | Owns optional Agent components | Window lifecycle | UIA and loopback live acceptance | Agent mode off |
| Source/Processors/PluginManager/PluginManager.cpp | MODIFIED | Loads configured plugins | Optional no-user-plugins isolation | Plugin availability in Agent test mode | runtime isolation acceptance | Omit no-user-plugins flag |
| Source/Processors/PluginManager/PluginManager.h | MODIFIED | Loads configured plugins | Declares isolation behavior | Plugin availability in Agent test mode | runtime isolation acceptance | Omit no-user-plugins flag |
| Source/Processors/ProcessorGraph/ProcessorGraph.cpp | MODIFIED | Official graph dispatch | Publishes bounded observed transport state | Observation overhead | state and Release tests | Agent mode off |
| Source/Processors/ProcessorGraph/ProcessorGraph.h | MODIFIED | Official graph dispatch | Declares Agent observation integration | Observation overhead | state and Release tests | Agent mode off |
| Source/UI/ControlPanel.cpp | MODIFIED | Official acquisition/record buttons and filename fields | Routes semantic origins, adds exact directory preparation, and clears stale stored prepend/append values when selecting none | Control and naming parity | command routing, directory adapter contract, and GUI acceptance | Build official tag |
| Source/UI/ControlPanel.h | MODIFIED | Official acquisition/record buttons | Declares Agent-aware routing hooks | Control parity | command routing and GUI acceptance | Agent mode off |
| Source/UI/UIComponent.cpp | MODIFIED | Official root UI | Adds optional read-only Agent accessibility bridge | UI composition | UIA live acceptance | Agent UIA flag off |

## Explicit unchanged scientific boundaries

| Boundary | Class | Statement |
|---|---|---|
| PXI plugin modification | NONE | The installed Neuropixels-PXI 1.0.3-API10 plugin is not modified by this fork. |
| imec Neuropix API | UNCHANGED_BOUNDARY | API 3.70.3 binaries and calls are not modified. |
| Drivers, firmware, calibration | UNCHANGED_BOUNDARY | Phase 2 does not modify these components or their scientific values. |
| Record Engine | UNCHANGED_BOUNDARY | Native recording engines and write threads are not modified. |
| Binary format | UNCHANGED_BOUNDARY | Native hierarchy and file format remain official. |
| Sampling and signal processing | UNCHANGED_BOUNDARY | Sample transport and processor algorithms are not modified. |
