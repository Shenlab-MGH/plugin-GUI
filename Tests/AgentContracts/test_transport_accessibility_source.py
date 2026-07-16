from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MAIN_WINDOW = (ROOT / "Source" / "MainWindow.cpp").read_text(encoding="utf-8")
CONTROL_PANEL = (ROOT / "Source" / "UI" / "ControlPanel.cpp").read_text(
    encoding="utf-8"
)
REGISTRY = (
    ROOT / "Source" / "Agent" / "TransportAccessibilityRegistry.cpp"
).read_text(encoding="utf-8")


def require(fragment: str, source: str, message: str) -> None:
    if fragment not in source:
        raise AssertionError(message)


def main() -> None:
    require(
        "setAccessible (false);",
        MAIN_WINDOW,
        "The unaudited full GUI accessibility tree must remain disabled until an allowlist provider exists",
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

    print("PASS transport accessibility source contract")


if __name__ == "__main__":
    main()
