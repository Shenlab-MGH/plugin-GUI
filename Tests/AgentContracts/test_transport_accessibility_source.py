from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MAIN_WINDOW = (ROOT / "Source" / "MainWindow.cpp").read_text(encoding="utf-8")
CONTROL_PANEL = (ROOT / "Source" / "UI" / "ControlPanel.cpp").read_text(
    encoding="utf-8"
)


def require(fragment: str, source: str, message: str) -> None:
    if fragment not in source:
        raise AssertionError(message)


def main() -> None:
    require(
        "setAccessible (false);",
        MAIN_WINDOW,
        "The unaudited full GUI accessibility tree must remain disabled until an allowlist provider exists",
    )

    expected_control_metadata = {
        'setComponentID ("oe.transport.acquisition");':
            "Play must expose a stable semantic component ID",
        'setTitle ("Acquisition");':
            "Play must expose an accessible title",
        'setDescription ("Start or stop data acquisition");':
            "Play must expose an accessible description",
        'setHelpText ("Starts or stops data acquisition without changing recording settings.");':
            "Play must expose accessible help",
        'setTooltip ("Starts or stops data acquisition without changing recording settings.");':
            "Play must expose help through JUCE Button's accessibility handler",
        'setComponentID ("oe.transport.recording");':
            "Record must expose a stable semantic component ID",
        'setTitle ("Recording");':
            "Record must expose an accessible title",
        'setDescription ("Start or stop recording to disk");':
            "Record must expose an accessible description",
        'setHelpText ("Starts or stops recording using the existing recording safety checks.");':
            "Record must expose accessible help",
        'setTooltip ("Starts or stops recording using the existing recording safety checks.");':
            "Record must expose help through JUCE Button's accessibility handler",
    }

    for fragment, message in expected_control_metadata.items():
        require(fragment, CONTROL_PANEL, message)

    print("PASS transport accessibility source contract")


if __name__ == "__main__":
    main()
