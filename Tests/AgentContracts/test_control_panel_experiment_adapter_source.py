from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    path = ROOT / relative
    return path.read_text(encoding="utf-8") if path.exists() else ""


def main() -> None:
    header = read("Source/UI/ControlPanel.h")
    implementation = read("Source/UI/ControlPanel.cpp")
    adapter = read("Source/Agent/ControlPanelExperimentAdapter.cpp")
    endpoint = read("Source/Agent/AgentExperimentDirectoryEndpoint.cpp")
    server = read("Source/Agent/AgentLoopbackServer.cpp")
    protocol = read("Source/Agent/AgentControlProtocol.cpp")

    assert "getAgentRecordingDirectorySnapshot" in header
    assert "prepareAgentRecordingDirectory" in header
    assert "getAgentRecordingDirectorySnapshot" in implementation
    assert "prepareAgentRecordingDirectory" in implementation
    assert "MessageManager::getInstance" in implementation
    assert "isThisTheMessageThread" in implementation
    assert 'field->value = ""' in implementation
    assert "CoreServices::setRecordingStatus" not in adapter
    assert "AgentExperimentDirectoryEndpoint" in endpoint
    assert 'Put ("/v1/experiment/directory"' in server
    assert "serializeDirectorySnapshot" in protocol
    assert "parseDirectoryRequest" in protocol
    assert "DIRECTORY_COLLISION" in protocol
    print("PASS ControlPanel experiment adapter source contract")


if __name__ == "__main__":
    main()
