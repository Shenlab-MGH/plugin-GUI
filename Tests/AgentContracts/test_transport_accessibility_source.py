from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MAIN_WINDOW = (ROOT / "Source" / "MainWindow.cpp").read_text(encoding="utf-8")
CONTROL_PANEL = (ROOT / "Source" / "UI" / "ControlPanel.cpp").read_text(
    encoding="utf-8"
)
REGISTRY = (
    ROOT / "Source" / "Agent" / "TransportAccessibilityRegistry.cpp"
).read_text(encoding="utf-8")
BRIDGE_PATH = ROOT / "Source" / "Agent" / "AgentAccessibilityBridge.cpp"
BRIDGE = (
    BRIDGE_PATH.read_text(encoding="utf-8")
    if BRIDGE_PATH.exists()
    else ""
)
WINDOWS_PROVIDER = (
    ROOT
    / "JuceLibraryCode/modules/juce_gui_basics/native/accessibility/juce_AccessibilityElement_windows.cpp"
).read_text(encoding="utf-8")


def require(fragment: str, source: str, message: str) -> None:
    if fragment not in source:
        raise AssertionError(message)


def main() -> None:
    require('"oe.agent.root"', BRIDGE,
            "The bridge must expose one stable root")
    require("if (! interactive)", BRIDGE,
            "Read-only mode must keep transport actions absent")
    require("AccessibilityActionType::press", BRIDGE,
            "Interactive mode must expose only a semantic press action")
    require("AgentAccessibilityAction::targetFor", BRIDGE,
            "Interactive actions must use the fail-closed target planner")
    require("endpoint->submit", BRIDGE,
            "Interactive actions must use the verified transport endpoint")
    require(
        "std::make_unique<ReadOnlyTransportValue> (\n"
        "                      endpoint,\n"
        "                      tracker,",
        BRIDGE,
        "Value and Invoke handlers must retain independent shared endpoint copies",
    )
    require("AccessibilityTextValueInterface", BRIDGE,
            "Transport nodes must provide a read-only value")
    require("isReadOnly() const override", BRIDGE,
            "The UIA value must reject writes")
    require("modalResilientReadOnlyState", BRIDGE,
            "Read-only Agent UIA must remain enumerable behind modal dialogs")
    require("withFocusable().withAccessibleOffscreen()", BRIDGE,
            "Modal-resilient UIA state must be focusable and explicitly offscreen")
    require("FocusContainerType::focusContainer", BRIDGE,
            "The Agent root must own its two transport nodes in the UIA tree")
    if BRIDGE.count("modalResilientReadOnlyState()") != 3:
        raise AssertionError(
            "The modal-resilient state must serve the root and both transport nodes"
        )
    require("runtimeOptions.enableAgentUiaReadOnly", MAIN_WINDOW,
            "The read-only branch must be explicitly enabled")
    require("runtimeOptions.enableAgentUiaInteractive", MAIN_WINDOW,
            "The interactive branch must be explicitly enabled")
    require("ui->setAccessible (false)", MAIN_WINDOW,
            "The ordinary editor subtree must remain hidden")
    require("enforceAgentAccessibilityBoundary", MAIN_WINDOW,
            "Window chrome must be hidden behind an explicit boundary")
    require('child->getComponentID() != "oe.agent.root"', MAIN_WINDOW,
            "Only the narrow Agent bridge may remain accessible")
    require('setComponentID ("oe.agent.window")', MAIN_WINDOW,
            "The restricted fragment root must have a stable identity")
    require("return componentId;", WINDOWS_PROVIDER,
            "Explicit component IDs must become stable Windows AutomationIds")
    require("void MainDocumentWindow::lookAndFeelChanged()", MAIN_WINDOW,
            "Title-bar rebuilds must reapply the accessibility boundary")
    require("submitOutcomeName", BRIDGE,
            "Every interactive submission outcome must be audited")
    require('"Agent UIA request_id="', BRIDGE,
            "UIA actions must write a structured action audit entry")
    require('"Agent UIA terminal request_id="', BRIDGE,
            "UIA actions must write a correlated terminal audit entry")
    require("deadlineMonotonicMs", (
        ROOT / "Source/Agent/AgentTransportPlanner.h"
    ).read_text(encoding="utf-8"),
            "UIA requests must carry an execution deadline")
    require('"|REQUEST="', BRIDGE,
            "UIA values must expose request correlation and terminal state")

    if BRIDGE.count("addAndMakeVisible") != 2:
        raise AssertionError(
            "The Agent accessibility root must expose exactly two children"
        )

    expected_registry_metadata = {
        '"oe.transport.acquisition"':
            "The registry must define the acquisition AutomationId",
        '"Acquisition"':
            "The registry must define the acquisition name",
        '"Start or stop data acquisition"':
            "The registry must define the acquisition description",
        '"oe.transport.recording"':
            "The registry must define the recording AutomationId",
        '"Recording"':
            "The registry must define the recording name",
        '"Start or stop recording to disk"':
            "The registry must define the recording description",
    }

    for fragment, message in expected_registry_metadata.items():
        require(fragment, REGISTRY, message)

    require(
        '#include "../Agent/TransportAccessibilityRegistry.h"',
        CONTROL_PANEL,
        "ControlPanel must consume the transport metadata registry",
    )

    if CONTROL_PANEL.count("applyTransportAccessibilityMetadata") != 3:
        raise AssertionError(
            "Play and Record must both use the shared metadata helper"
        )

    require(
        "TransportControlKind::acquisition",
        CONTROL_PANEL,
        "Play must bind by strongly typed control kind",
    )
    require(
        "TransportControlKind::recording",
        CONTROL_PANEL,
        "Record must bind by strongly typed control kind",
    )
    require(
        "button.setAccessible (false)",
        CONTROL_PANEL,
        "The real Play and Record buttons must default to inaccessible",
    )
    if "setAgentTransportControlsAccessible" in CONTROL_PANEL:
        raise AssertionError(
            "Interactive mode must not expose the native GUI control tree"
        )

    print("PASS transport accessibility source contract")


if __name__ == "__main__":
    main()
