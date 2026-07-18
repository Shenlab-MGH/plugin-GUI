#include "../../Source/Agent/AgentAccessibilityAction.h"

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
    using AgentAccessibilityAction::targetFor;

    require (targetFor (TransportControlKind::acquisition,
                        AgentObservedMode::idle)
                 == AgentObservedMode::acquire,
             "Acquisition press from IDLE must target ACQUIRE");
    require (targetFor (TransportControlKind::acquisition,
                        AgentObservedMode::acquire)
                 == AgentObservedMode::idle,
             "Acquisition press from ACQUIRE must target IDLE");
    require (targetFor (TransportControlKind::acquisition,
                        AgentObservedMode::record)
                 == AgentObservedMode::idle,
             "Acquisition press from RECORD must safely target IDLE");
    require (targetFor (TransportControlKind::recording,
                        AgentObservedMode::idle)
                 == AgentObservedMode::record,
             "Recording press from IDLE must target RECORD");
    require (targetFor (TransportControlKind::recording,
                        AgentObservedMode::acquire)
                 == AgentObservedMode::record,
             "Recording press from ACQUIRE must target RECORD");
    require (targetFor (TransportControlKind::recording,
                        AgentObservedMode::record)
                 == AgentObservedMode::acquire,
             "Recording press from RECORD must target ACQUIRE");
    require (! targetFor (TransportControlKind::acquisition,
                          AgentObservedMode::unknown).has_value(),
             "Unknown state must fail closed");

    std::cout << "PASS AgentAccessibilityActionTests\n";
    return 0;
}
