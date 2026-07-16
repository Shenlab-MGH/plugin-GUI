from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = (ROOT / "Source" / "UI" / "ControlPanel.h").read_text(
    encoding="utf-8"
)
IMPLEMENTATION = (ROOT / "Source" / "UI" / "ControlPanel.cpp").read_text(
    encoding="utf-8"
)


def require(fragment: str, source: str, message: str) -> None:
    if fragment not in source:
        raise AssertionError(message)


def main() -> None:
    header_contract = {
        '#include "../Agent/ControlPanelCommandRouter.h"':
            "ControlPanel must include the typed command router",
        "private AgentCommandDispatcher":
            "ControlPanel must implement the command dispatcher privately",
        "void dispatch (const AgentCommand& command) override;":
            "ControlPanel must implement typed command dispatch",
        "void handleAcquisitionToggleRequest();":
            "Acquisition behavior must have a focused dispatch handler",
        "void handleRecordingToggleRequest();":
            "Recording behavior must have a focused dispatch handler",
        "ControlPanelCommandRouter commandRouter;":
            "ControlPanel must own one command router",
    }

    implementation_contract = {
        "commandRouter (*this)":
            "The router must dispatch back into the owning ControlPanel",
        "commandRouter.requestAcquisitionToggle (":
            "The Play button must submit a typed command",
        "commandRouter.requestRecordingToggle (":
            "The Record button must submit a typed command",
        "void ControlPanel::dispatch (const AgentCommand& command)":
            "ControlPanel must dispatch typed commands",
        "void ControlPanel::handleAcquisitionToggleRequest()":
            "The existing acquisition branch must live behind the dispatcher",
        "void ControlPanel::handleRecordingToggleRequest()":
            "The existing recording branch must live behind the dispatcher",
    }

    for fragment, message in header_contract.items():
        require(fragment, HEADER, message)

    for fragment, message in implementation_contract.items():
        require(fragment, IMPLEMENTATION, message)

    if IMPLEMENTATION.count("AgentCommandOrigin::userInterface") != 2:
        raise AssertionError(
            "Both native transport buttons must use the honest shared UI origin"
        )

    print("PASS transport command routing source contract")


if __name__ == "__main__":
    main()
