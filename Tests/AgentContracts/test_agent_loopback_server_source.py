from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = (
    ROOT / "Source" / "Agent" / "AgentLoopbackServer.h"
).read_text(encoding="utf-8")
IMPLEMENTATION = (
    ROOT / "Source" / "Agent" / "AgentLoopbackServer.cpp"
).read_text(encoding="utf-8")
MAIN_HEADER = (ROOT / "Source" / "MainWindow.h").read_text(
    encoding="utf-8"
)
MAIN_IMPLEMENTATION = (ROOT / "Source" / "MainWindow.cpp").read_text(
    encoding="utf-8"
)
CONTROL_PANEL = (
    ROOT / "Source" / "UI" / "ControlPanel.cpp"
).read_text(encoding="utf-8")


def require(fragment: str, source: str, message: str) -> None:
    if fragment not in source:
        raise AssertionError(message)


def main() -> None:
    implementation_contract = {
        '"127.0.0.1"':
            "Agent server must bind only to IPv4 loopback",
        "server.listen_after_bind()":
            "Agent server must listen only after verified synchronous bind",
        '"Authorization"':
            "Every API route must require bearer authentication",
        "constantTimeEqual":
            "Bearer comparison must avoid ordinary early-exit equality",
        'server.Get ("/v1/status"':
            "The server must expose preview status",
        'R"(/v1/transport/requests/(.+))"':
            "The server must expose request lookup",
        'server.Post ("/v1/transport/requests"':
            "The server must expose target-state submission",
        "endpoint->serviceSnapshot()":
            "Status must come from the shared endpoint",
        "endpoint->submit (*parsed.request)":
            "Mutation must enter the safe endpoint",
        "endpoint->query (requestId)":
            "Result lookup must use the endpoint",
        '"MUTATION_NOT_ARMED"':
            "The server must support a fail-closed observe-only mode",
        "parsed.expectedSessionId":
            "Mutation must be scoped to the current process session",
    }
    for fragment, message in implementation_contract.items():
        require(fragment, IMPLEMENTATION, message)

    main_contract = {
        "std::unique_ptr<AgentLoopbackServer> agentServer;":
            "MainWindow must own the loopback server",
        'SystemStats::getEnvironmentVariable ("OE_AGENT_TOKEN", "")':
            "Server startup must require an explicit environment token",
        "controlPanel->getAgentTransportEndpoint()":
            "MainWindow must inject the shared endpoint",
        "agentServer->start()":
            "MainWindow must start the server explicitly",
        "agentServer->stop();":
            "MainWindow teardown must stop the server explicitly",
        "37498,\n            false":
            "The v0.0.1 host integration must remain observe-only",
    }
    combined_main = MAIN_HEADER + MAIN_IMPLEMENTATION
    for fragment, message in main_contract.items():
        require(fragment, combined_main, message)

    fault_section = CONTROL_PANEL[
        CONTROL_PANEL.index("void ControlPanel::disableCallbacks()"):
        CONTROL_PANEL.index("void ControlPanel::timerCallback()")
    ]
    if (
        "clock->stop();" not in fault_section
        or "readState();" not in fault_section
    ):
        raise AssertionError(
            "Fault-driven callback shutdown must publish authoritative state"
        )

    timer_section = CONTROL_PANEL[
        CONTROL_PANEL.index("void ControlPanel::timerCallback()"):
        CONTROL_PANEL.index("void ControlPanel::refreshMeters()")
    ]
    if (
        "refreshMeters();" not in timer_section
        or "readState();" not in timer_section
    ):
        raise AssertionError(
            "The UI timer must periodically reconcile external state"
        )

    forbidden = {
        "CoreServices::setAcquisitionStatus":
            "Agent server must not use legacy force/toggle APIs",
        "CoreServices::setRecordingStatus":
            "Agent server must not use legacy force-record APIs",
        'listen ("0.0.0.0"':
            "Agent server must never wildcard-bind",
        "AccessClass::getControlPanel":
            "Agent server must not dereference the global ControlPanel",
    }
    for fragment, message in forbidden.items():
        if fragment in IMPLEMENTATION:
            raise AssertionError(message)

    print("PASS agent loopback server source contract")


if __name__ == "__main__":
    main()
