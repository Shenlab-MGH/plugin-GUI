from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (
    ROOT
    / "JuceLibraryCode"
    / "modules"
    / "juce_gui_basics"
    / "native"
    / "accessibility"
    / "juce_AccessibilityElement_windows.cpp"
).read_text(encoding="utf-8")


def main() -> None:
    start = SOURCE.index("static String getAutomationId")
    end = SOURCE.index("static auto roleToControlTypeId", start)
    function = SOURCE[start:end]

    required = [
        "handler.getComponent().getComponentID()",
        "componentId.isNotEmpty()",
        "return componentId;",
        "auto result = handler.getTitle();",
    ]
    for fragment in required:
        if fragment not in function:
            raise AssertionError(
                f"Windows AutomationId policy is missing: {fragment}"
            )

    if function.index("return componentId;") > function.index(
        "auto result = handler.getTitle();"
    ):
        raise AssertionError(
            "ComponentID must be returned before the title fallback"
        )

    if 'result << "."' not in function:
        raise AssertionError(
            "Empty ComponentID must retain the title hierarchy fallback"
        )

    print("PASS Windows AutomationId source contract")


if __name__ == "__main__":
    main()
