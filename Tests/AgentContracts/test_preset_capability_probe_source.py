from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    source = (
        ROOT / "tools/windows/Test-NeuropixelsPresetCapability.ps1"
    ).read_text(encoding="utf-8")
    assert "UIAutomationClient" in source
    assert "ValuePattern" in source
    assert "plugin_sha256" in source
    assert "exact_displayed_preset_texts" in source
    assert "documented_semantic_readback = $false" in source
    assert "accessibility_set_verified = $false" in source
    assert "classification = 'UNVERIFIABLE'" in source
    for forbidden in (
        "InvokePattern",
        "SetValue",
        "SendKeys",
        "mouse_event",
        "Click()",
    ):
        assert forbidden not in source
    print("PASS preset capability probe source contract")


if __name__ == "__main__":
    main()
