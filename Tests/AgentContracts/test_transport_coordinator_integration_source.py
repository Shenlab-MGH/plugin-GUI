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
        '#include "../Agent/AgentTransportCoordinator.h"':
            "ControlPanel must include the verified coordinator",
        "private AgentTransportRuntime":
            "ControlPanel must implement the transport runtime privately",
        "getAgentTransportEndpoint() const;":
            "Worker adapters need the shared lifetime-safe endpoint",
        "std::shared_ptr<AgentTransportEndpoint> agentTransportEndpoint;":
            "ControlPanel must retain the endpoint snapshot cache",
        "AgentTransportApplyResult applyAgentTransportRequest (":
            "Internal adapters need the coordinator entrypoint",
        "bool agentTransportTransactionActive = false;":
            "ControlPanel must reject re-entrant transport transactions",
        "agentActiveDeadlineMonotonicMs":
            "ControlPanel must retain the active monotonic deadline",
        "AgentStateSnapshot readState() override;":
            "The coordinator must read authoritative ControlPanel state",
        "AgentObservedMode getAuthoritativeAgentMode();":
            "Agent state must come from runtime components, not button toggles",
        "bool execute (AgentTransportAction action) override;":
            "The coordinator must use an explicit high-level executor",
        "AgentStateStore agentStateStore;":
            "ControlPanel must own the observed revision state",
        "AgentTransportCoordinator agentTransportCoordinator;":
            "ControlPanel must own the verified coordinator",
    }

    implementation_contract = {
        "AgentTransportApplyResult ControlPanel::applyAgentTransportRequest":
            "Requests must enter the coordinator",
        "agentTransportCoordinator.apply (request, *this)":
            "ControlPanel must not bypass the coordinator",
        "isThisTheMessageThread()":
            "Transport mutation must be restricted to the JUCE message thread",
        "AgentTransportApplyOutcome::busy":
            "A concurrent or re-entrant transaction must fail closed",
        "request.deadlineMonotonicMs":
            "The native mutation transaction must bind the request deadline",
        "monotonicMilliseconds()":
            "The native start entry must re-check the monotonic deadline",
        "AgentStateSnapshot ControlPanel::readState()":
            "ControlPanel must implement authoritative readback",
        "ControlPanel::getAgentTransportEndpoint() const":
            "The shared endpoint getter must have an explicit implementation",
        "agentTransportEndpoint->publish (state)":
            "Message-thread readback must publish coherent snapshots",
        "audio->callbacksAreActive()":
            "Acquisition readback must inspect active audio callbacks",
        "node->getRecordingStatus()":
            "Recording readback must inspect Record Node state",
        "bool ControlPanel::execute (AgentTransportAction action)":
            "ControlPanel must implement high-level actions",
        "case AgentTransportAction::requestSafeRecordingStart:":
            "Recording must have a distinct safe action",
        "startAcquisition (true);":
            "Safe recording from IDLE must use the complete acquisition path",
        "startRecording();":
            "Safe recording from ACQUIRE must use the ControlPanel high-level path",
        "forceRecording = false;":
            "Agent recording must clear the inherited force-record bypass",
        "graph->allRecordNodeDirectoriesAreValid()":
            "Agent recording must validate directories from every source state",
        "graph->allRecordNodesAreSynchronized()":
            "Agent recording must fail closed when streams are unsynchronized",
        "playButton->setToggleState (true, dontSendNotification);":
            "Agent acquisition start must mirror the GUI toggle precondition",
        "playButton->setToggleState (false, dontSendNotification);":
            "Agent acquisition stop must mirror the GUI toggle precondition",
    }

    for fragment, message in header_contract.items():
        require(fragment, HEADER, message)

    for fragment, message in implementation_contract.items():
        require(fragment, IMPLEMENTATION, message)

    forbidden = {
        "CoreServices::setRecordingStatus":
            "The coordinator adapter must not use the force-record API",
        "graph->setRecordState":
            "The coordinator adapter must not call the low-level record primitive",
    }

    coordinator_section = IMPLEMENTATION[
        IMPLEMENTATION.find(
            "AgentTransportApplyResult ControlPanel::applyAgentTransportRequest"
        ):
        IMPLEMENTATION.find("void ControlPanel::buttonClicked")
    ]

    for fragment, message in forbidden.items():
        if fragment in coordinator_section:
            raise AssertionError(message)

    print("PASS transport coordinator integration source contract")


if __name__ == "__main__":
    main()
