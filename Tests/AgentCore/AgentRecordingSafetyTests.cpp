#include "../../Source/Agent/AgentRecordingSafety.h"

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

AgentRecordingGateObservation readyObservation()
{
    return {
        true,
        true,
        true,
        true,
        AgentObservedMode::acquire,
        9
    };
}
}

int main()
{
    AgentRecordingSafety gate;
    const AgentRecordingGateContext agent { true, 9 };

    require (gate.evaluate (agent, readyObservation()).outcome
                 == AgentRecordingGateOutcome::ready,
             "Ready synchronized Agent state must pass");

    auto unsynchronized = readyObservation();
    unsynchronized.synchronized = false;
    require (gate.evaluate (agent, unsynchronized).outcome
                 == AgentRecordingGateOutcome::syncBlocked,
             "Unsynchronized streams must block Agent recording");

    auto noDirectory = readyObservation();
    noDirectory.directoryPrepared = false;
    require (gate.evaluate (agent, noDirectory).outcome
                 == AgentRecordingGateOutcome::directoryNotPrepared,
             "Agent recording requires exact prepared directory readback");

    auto noNodes = readyObservation();
    noNodes.hasRecordNodes = false;
    require (gate.evaluate (agent, noNodes).outcome
                 == AgentRecordingGateOutcome::recordNodesNotReady,
             "Missing Record Nodes must block recording");

    auto unknown = readyObservation();
    unknown.mode = AgentObservedMode::unknown;
    require (gate.evaluate (agent, unknown).outcome
                 == AgentRecordingGateOutcome::stateUnknown,
             "Unknown native state must block recording");

    auto stale = readyObservation();
    stale.revision = 10;
    require (gate.evaluate (agent, stale).outcome
                 == AgentRecordingGateOutcome::revisionConflict,
             "Stale Agent revision must block recording");

    const AgentRecordingGateContext human { false, 0 };
    noDirectory.synchronized = true;
    require (gate.evaluate (human, noDirectory).outcome
                 == AgentRecordingGateOutcome::ready,
             "Human recording must preserve official directory workflow");
    require (gate.evaluate (human, unsynchronized).outcome
                 == AgentRecordingGateOutcome::syncBlocked,
             "Human and Agent paths must share synchronization detection");

    std::cout << "PASS AgentRecordingSafetyTests\n";
    return 0;
}
