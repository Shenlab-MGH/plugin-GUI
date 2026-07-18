from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def main() -> None:
    header = read("Source/UI/ControlPanel.h")
    implementation = read("Source/UI/ControlPanel.cpp")
    agent_sources = "\n".join(
        path.read_text(encoding="utf-8")
        for path in (ROOT / "Source/Agent").glob("*.cpp")
    )

    assert "requestValidatedRecordingStart" in header
    assert implementation.count("requestValidatedRecordingStart (") >= 3
    assert "AgentRecordingSafety" in implementation
    assert "Agent recording blocked: data streams not synchronized" in implementation
    assert "Agent recording blocked: request deadline expired" in implementation
    deadline_guard = implementation.index(
        "Agent recording blocked: request deadline expired"
    )
    native_start = implementation.index(
        "if (playButton->getToggleState())",
        deadline_guard,
    )
    assert deadline_guard < native_start
    assert "CoreServices::setRecordingStatus (true" not in agent_sources
    assert "forceRecording = true" not in agent_sources
    print("PASS recording safety integration source contract")


if __name__ == "__main__":
    main()
