#include "../../Source/Agent/AgentTransportPlanner.h"

#include <cstdlib>
#include <iostream>

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
}

int main()
{
    AgentTransportPlanner planner;

    const AgentStateSnapshot acquiring {
        "1.0.2-agent",
        AgentObservedMode::acquire,
        7
    };

    const AgentTransportRequest alreadyAcquiring {
        "request-1",
        AgentObservedMode::acquire,
        6
    };

    const auto alreadySatisfied = planner.plan (
        acquiring,
        alreadyAcquiring);
    require (alreadySatisfied.outcome
                 == AgentTransportPlanOutcome::alreadySatisfied,
             "A repeated target state must be idempotently satisfied");
    require (alreadySatisfied.steps.empty(),
             "An already-satisfied request must perform no action");

    const AgentTransportRequest staleRequest {
        "request-2",
        AgentObservedMode::record,
        6
    };

    const auto revisionConflict = planner.plan (acquiring, staleRequest);
    require (revisionConflict.outcome
                 == AgentTransportPlanOutcome::revisionConflict,
             "A stale expected revision must be rejected");
    require (revisionConflict.steps.empty(),
             "A revision conflict must perform no action");

    const AgentStateSnapshot unknown {
        "1.0.2-agent",
        AgentObservedMode::unknown,
        0
    };

    const AgentTransportRequest fromUnknown {
        "request-3",
        AgentObservedMode::idle,
        0
    };

    require (planner.plan (unknown, fromUnknown).outcome
                 == AgentTransportPlanOutcome::stateUnknown,
             "Commands must be rejected while observed state is unknown");

    const AgentTransportRequest beginRecording {
        "request-4",
        AgentObservedMode::record,
        7
    };

    const auto recordPlan = planner.plan (acquiring, beginRecording);
    require (recordPlan.outcome
                 == AgentTransportPlanOutcome::proposalReady,
             "ACQUIRE to RECORD must produce a transition proposal");
    require (recordPlan.steps.size() == 1
                 && recordPlan.steps[0].action
                        == AgentTransportAction::requestSafeRecordingStart,
             "ACQUIRE to RECORD must request the safe recording gate");
    require (recordPlan.steps[0].expectedBefore
                 == AgentObservedMode::acquire
                 && recordPlan.steps[0].expectedAfter
                        == AgentObservedMode::record,
             "A recording proposal must carry readback conditions");
    require (recordPlan.steps[0].requiresRecordingSafetyGate,
             "A recording proposal must explicitly require safety checks");

    const AgentStateSnapshot idle {
        "1.0.2-agent",
        AgentObservedMode::idle,
        3
    };
    const AgentTransportRequest recordFromIdle {
        "request-idle-record",
        AgentObservedMode::record,
        3
    };
    const auto idleToRecord = planner.plan (idle, recordFromIdle);
    require (idleToRecord.outcome
                 == AgentTransportPlanOutcome::proposalReady,
             "IDLE to RECORD must produce a transition proposal");
    require (idleToRecord.steps.size() == 1
                 && idleToRecord.steps[0].action
                        == AgentTransportAction::requestSafeRecordingStart,
             "IDLE to RECORD must use the atomic safe recording path");
    require (idleToRecord.steps[0].expectedBefore
                 == AgentObservedMode::idle
                 && idleToRecord.steps[0].expectedAfter
                        == AgentObservedMode::record,
             "IDLE to RECORD must verify the complete transition");

    const AgentTransportRequest acquireFromIdle {
        "request-idle-acquire",
        AgentObservedMode::acquire,
        3
    };
    const auto idleToAcquire = planner.plan (idle, acquireFromIdle);
    require (idleToAcquire.outcome
                 == AgentTransportPlanOutcome::proposalReady
                 && idleToAcquire.steps.size() == 1
                 && idleToAcquire.steps[0].action
                        == AgentTransportAction::startAcquisition,
             "IDLE to ACQUIRE must start acquisition");

    const AgentTransportRequest idleFromAcquire {
        "request-acquire-idle",
        AgentObservedMode::idle,
        7
    };
    const auto acquireToIdle = planner.plan (acquiring, idleFromAcquire);
    require (acquireToIdle.outcome
                 == AgentTransportPlanOutcome::proposalReady
                 && acquireToIdle.steps.size() == 1
                 && acquireToIdle.steps[0].action
                        == AgentTransportAction::stopAcquisition,
             "ACQUIRE to IDLE must stop acquisition");

    const AgentStateSnapshot recording {
        "1.0.2-agent",
        AgentObservedMode::record,
        8
    };

    const AgentTransportRequest returnIdle {
        "request-5",
        AgentObservedMode::idle,
        8
    };

    const auto idlePlan = planner.plan (recording, returnIdle);
    require (idlePlan.outcome
                 == AgentTransportPlanOutcome::proposalReady,
             "RECORD to IDLE must produce a transition proposal");
    require (idlePlan.steps.size() == 2,
             "RECORD to IDLE must use an ordered two-step shutdown");
    require (idlePlan.steps[0].action
                 == AgentTransportAction::stopRecording,
             "RECORD to IDLE must stop recording first");
    require (idlePlan.steps[0].expectedAfter
                 == AgentObservedMode::acquire,
             "Stopping recording must read back ACQUIRE before continuing");
    require (idlePlan.steps[1].action
                 == AgentTransportAction::stopAcquisition,
             "RECORD to IDLE must stop acquisition second");
    require (idlePlan.steps[1].expectedBefore
                 == AgentObservedMode::acquire
                 && idlePlan.steps[1].expectedAfter
                        == AgentObservedMode::idle,
             "The second stop step must require the first postcondition");
    require (idlePlan.requestId == "request-5"
                 && idlePlan.sourceMode == AgentObservedMode::record
                 && idlePlan.targetMode == AgentObservedMode::idle
                 && idlePlan.expectedRevision == 8,
             "A proposal must retain its complete planning context");

    const AgentTransportRequest acquireFromRecord {
        "request-record-acquire",
        AgentObservedMode::acquire,
        8
    };
    const auto recordToAcquire = planner.plan (
        recording,
        acquireFromRecord);
    require (recordToAcquire.outcome
                 == AgentTransportPlanOutcome::proposalReady
                 && recordToAcquire.steps.size() == 1
                 && recordToAcquire.steps[0].action
                        == AgentTransportAction::stopRecording,
             "RECORD to ACQUIRE must stop recording");
    require (recordToAcquire.steps[0].expectedBefore
                 == AgentObservedMode::record
                 && recordToAcquire.steps[0].expectedAfter
                        == AgentObservedMode::acquire,
             "RECORD to ACQUIRE must verify its readback state");

    const AgentTransportRequest missingId {
        "",
        AgentObservedMode::record,
        8
    };
    require (planner.plan (recording, missingId).outcome
                 == AgentTransportPlanOutcome::invalidRequest,
             "Every remote transport request must have an audit ID");

    std::cout << "PASS AgentTransportPlannerTests\n";
    return 0;
}
