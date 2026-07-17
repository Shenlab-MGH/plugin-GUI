#include "../../Source/Agent/AgentAccessibilityState.h"

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
    using namespace AgentAccessibilityState;

    require (acquisition (AgentObservedMode::idle)
                 == AgentAccessibilityValue::off,
             "IDLE acquisition must be OFF");
    require (acquisition (AgentObservedMode::acquire)
                 == AgentAccessibilityValue::on,
             "ACQUIRE acquisition must be ON");
    require (acquisition (AgentObservedMode::record)
                 == AgentAccessibilityValue::on,
             "RECORD acquisition must be ON");
    require (acquisition (AgentObservedMode::unknown)
                 == AgentAccessibilityValue::unknown,
             "UNKNOWN acquisition must remain UNKNOWN");

    require (recording (AgentObservedMode::idle)
                 == AgentAccessibilityValue::off,
             "IDLE recording must be OFF");
    require (recording (AgentObservedMode::acquire)
                 == AgentAccessibilityValue::off,
             "ACQUIRE recording must be OFF");
    require (recording (AgentObservedMode::record)
                 == AgentAccessibilityValue::on,
             "RECORD recording must be ON");
    require (recording (AgentObservedMode::unknown)
                 == AgentAccessibilityValue::unknown,
             "UNKNOWN recording must remain UNKNOWN");

    require (toString (AgentAccessibilityValue::off) == "OFF",
             "OFF text must be stable");
    require (toString (AgentAccessibilityValue::on) == "ON",
             "ON text must be stable");
    require (toString (AgentAccessibilityValue::unknown) == "UNKNOWN",
             "UNKNOWN text must be stable");

    std::cout << "PASS AgentAccessibilityStateTests\n";
    return 0;
}
