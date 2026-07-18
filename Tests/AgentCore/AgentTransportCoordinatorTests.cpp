#include "../../Source/Agent/AgentTransportCoordinator.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
void require (bool condition, const char* message)
{
    if (! condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit (1);
    }
}

class FakeRuntime final : public AgentTransportRuntime
{
public:
    AgentStateSnapshot readState() override
    {
        if (readIndex < scriptedStates.size())
            current = scriptedStates[readIndex++];

        return current;
    }

    bool execute (AgentTransportAction action) override
    {
        executed.push_back (action);
        return executeResult;
    }

    AgentStateSnapshot current {
        "1.0.2-agent",
        AgentObservedMode::unknown,
        0
    };
    std::vector<AgentStateSnapshot> scriptedStates;
    std::size_t readIndex = 0;
    std::vector<AgentTransportAction> executed;
    bool executeResult = true;
};
}

int main()
{
    AgentTransportCoordinator coordinator;

    FakeRuntime successful;
    successful.scriptedStates = {
        { "1.0.2-agent", AgentObservedMode::record, 8 },
        { "1.0.2-agent", AgentObservedMode::record, 8 },
        { "1.0.2-agent", AgentObservedMode::acquire, 9 },
        { "1.0.2-agent", AgentObservedMode::acquire, 9 },
        { "1.0.2-agent", AgentObservedMode::idle, 10 }
    };
    const AgentTransportRequest stopAll {
        "coordinator-success",
        AgentObservedMode::idle,
        8
    };

    const auto success = coordinator.apply (stopAll, successful);
    require (success.outcome
                 == AgentTransportApplyOutcome::completed,
             "A fully verified transition must complete");
    require (successful.executed.size() == 2,
             "RECORD to IDLE must execute exactly two actions");
    require (successful.executed[0]
                 == AgentTransportAction::stopRecording
                 && successful.executed[1]
                        == AgentTransportAction::stopAcquisition,
             "The coordinator must preserve the safe stop order");

    FakeRuntime staleBeforeExecution;
    staleBeforeExecution.scriptedStates = {
        { "1.0.2-agent", AgentObservedMode::acquire, 4 },
        { "1.0.2-agent", AgentObservedMode::idle, 5 }
    };
    const AgentTransportRequest stale {
        "coordinator-stale",
        AgentObservedMode::record,
        4
    };
    const auto staleResult = coordinator.apply (
        stale,
        staleBeforeExecution);
    require (staleResult.outcome
                 == AgentTransportApplyOutcome::preconditionChanged,
             "A state change after planning must cancel execution");
    require (staleBeforeExecution.executed.empty(),
             "A stale proposal must execute no action");

    FakeRuntime failedFirstStep;
    failedFirstStep.scriptedStates = {
        { "1.0.2-agent", AgentObservedMode::record, 12 },
        { "1.0.2-agent", AgentObservedMode::record, 12 },
        { "1.0.2-agent", AgentObservedMode::acquire, 13 }
    };
    failedFirstStep.executeResult = false;
    const AgentTransportRequest stopFailure {
        "coordinator-step-failure",
        AgentObservedMode::idle,
        12
    };
    const auto failed = coordinator.apply (
        stopFailure,
        failedFirstStep);
    require (failed.outcome
                 == AgentTransportApplyOutcome::executionFailed,
             "A failed action must fail the request");
    require (failedFirstStep.executed.size() == 1,
             "A failed first action must prevent the second action");
    require (failed.finalState.mode == AgentObservedMode::acquire,
             "Execution failure must return a fresh readback, not stale state");

    FakeRuntime badReadback;
    badReadback.scriptedStates = {
        { "1.0.2-agent", AgentObservedMode::acquire, 20 },
        { "1.0.2-agent", AgentObservedMode::acquire, 20 },
        { "1.0.2-agent", AgentObservedMode::acquire, 20 }
    };
    const AgentTransportRequest recordRequest {
        "coordinator-readback",
        AgentObservedMode::record,
        20
    };
    const auto readbackFailure = coordinator.apply (
        recordRequest,
        badReadback);
    require (readbackFailure.outcome
                 == AgentTransportApplyOutcome::readbackMismatch,
             "Success must require the expected post-state");
    require (badReadback.executed.size() == 1
                 && badReadback.executed[0]
                        == AgentTransportAction::requestSafeRecordingStart,
             "Recording must use only the safe recording action");

    FakeRuntime expiredStart;
    expiredStart.scriptedStates = {
        { "1.0.2-agent", AgentObservedMode::idle, 30 },
        { "1.0.2-agent", AgentObservedMode::idle, 30 }
    };
    const AgentTransportRequest expiredStartRequest {
        "coordinator-expired-start",
        AgentObservedMode::acquire,
        30,
        AgentObservedMode::idle,
        {}, {}, {}, {},
        1
    };
    const auto expiredStartResult = coordinator.apply (
        expiredStartRequest,
        expiredStart);
    require (expiredStartResult.outcome
                 == AgentTransportApplyOutcome::rejected,
             "An expired start request must fail closed at commit");
    require (expiredStart.executed.empty(),
             "An expired start request must execute no action");

    FakeRuntime expiredStop;
    expiredStop.scriptedStates = {
        { "1.0.2-agent", AgentObservedMode::acquire, 40 },
        { "1.0.2-agent", AgentObservedMode::acquire, 40 },
        { "1.0.2-agent", AgentObservedMode::idle, 41 }
    };
    const AgentTransportRequest expiredStopRequest {
        "coordinator-expired-stop",
        AgentObservedMode::idle,
        40,
        AgentObservedMode::acquire,
        {}, {}, {}, {},
        1
    };
    const auto expiredStopResult = coordinator.apply (
        expiredStopRequest,
        expiredStop);
    require (expiredStopResult.outcome
                 == AgentTransportApplyOutcome::completed,
             "An expired failsafe stop must remain available");
    require (expiredStop.executed.size() == 1
                 && expiredStop.executed[0]
                        == AgentTransportAction::stopAcquisition,
             "Expired requests may execute only the safe stop action");

    std::cout << "PASS AgentTransportCoordinatorTests\n";
    return 0;
}
