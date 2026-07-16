from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = (ROOT / "Source" / "UI" / "ControlPanel.h").read_text(
    encoding="utf-8"
)
IMPLEMENTATION = (ROOT / "Source" / "UI" / "ControlPanel.cpp").read_text(
    encoding="utf-8"
)
ACCESS_HEADER = (ROOT / "Source" / "AccessClass.h").read_text(
    encoding="utf-8"
)
ACCESS_IMPLEMENTATION = (ROOT / "Source" / "AccessClass.cpp").read_text(
    encoding="utf-8"
)


def require(fragment: str, source: str, message: str) -> None:
    if fragment not in source:
        raise AssertionError(message)


def main() -> None:
    header_contract = {
        '#include "../Agent/AgentTransportEndpoint.h"':
            "ControlPanel must use the shared lifetime-safe endpoint",
        "std::shared_ptr<AgentTransportEndpoint> "
        "getAgentTransportEndpoint() const;":
            "Worker adapters must receive a shared endpoint, not ControlPanel",
        "std::shared_ptr<AgentTransportEndpoint> agentTransportEndpoint;":
            "ControlPanel must retain the shared endpoint",
        "std::shared_ptr<ControlPanelTransportExecutor> "
        "agentTransportExecutor;":
            "ControlPanel must retain its message-thread executor lease",
    }

    implementation_contract = {
        "AgentTransportEndpoint::create (":
            "ControlPanel must create the independent shared endpoint",
        "agentTransportEndpoint->attachExecutor (":
            "The message-thread executor lease must be attached",
        "agentTransportEndpoint->detachExecutor (":
            "ControlPanel destruction must detach the executor lease",
        "agentTransportEndpoint->beginShutdown();":
            "ControlPanel shutdown must terminalize outstanding work",
        "AccessClass::clearControlPanel (this);":
            "ControlPanel teardown must not leave a global dangling pointer",
        "agentTransportEndpoint->publish (state);":
            "Authoritative state must be published through the endpoint",
        "ControlPanel::getAgentTransportEndpoint() const":
            "The shared endpoint accessor must be implemented",
    }

    for fragment, message in header_contract.items():
        require(fragment, HEADER, message)

    for fragment, message in implementation_contract.items():
        require(fragment, IMPLEMENTATION, message)

    require(
        "void clearControlPanel (ControlPanel* expected);",
        ACCESS_HEADER,
        "AccessClass must expose compare-and-clear for ControlPanel teardown",
    )
    require(
        "if (cp == expected)",
        ACCESS_IMPLEMENTATION,
        "AccessClass must only clear the expected ControlPanel instance",
    )

    forbidden_header_fragments = {
        "queueAgentTransportRequest":
            "Workers must not call a raw ControlPanel queue method",
        "getAgentTransportResult":
            "Workers must query the shared endpoint after ControlPanel teardown",
        "private AsyncUpdater":
            "The endpoint scheduler replaces ControlPanel-owned AsyncUpdater",
    }
    for fragment, message in forbidden_header_fragments.items():
        if fragment in HEADER:
            raise AssertionError(message)

    control_panel_start = HEADER.index("class TESTABLE ControlPanel")
    public_start = HEADER.index("public:", control_panel_start)
    private_start = HEADER.index("private:", public_start)
    if "applyAgentTransportRequest" in HEADER[public_start:private_start]:
        raise AssertionError(
            "Direct transport mutation must not remain public on ControlPanel"
        )

    dispatch_start = IMPLEMENTATION.index(
        "void ControlPanel::dispatch (const AgentCommand& command)"
    )
    dispatch_end = IMPLEMENTATION.index(
        "void ControlPanel::handleAcquisitionToggleRequest()",
        dispatch_start,
    )
    dispatch_body = IMPLEMENTATION[dispatch_start:dispatch_end]
    require(
        "readState();",
        dispatch_body,
        "Every human, UIA, HTTP, or Agent transport dispatch must publish "
        "authoritative state",
    )

    print("PASS transport mailbox integration source contract")


if __name__ == "__main__":
    main()
