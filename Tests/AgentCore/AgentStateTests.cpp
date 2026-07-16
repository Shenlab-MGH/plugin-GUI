#include "../../Source/Agent/AgentState.h"

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
    AgentStateStore store ("1.0.2-agent");

    const auto initial = store.snapshot();
    require (initial.guiVersion == "1.0.2-agent",
             "State snapshot must preserve the GUI version");
    require (initial.mode == AgentObservedMode::unknown,
             "State store must start in unknown mode");
    require (initial.revision == 0,
             "State store must start at revision zero");

    const auto acquiring = store.observe (AgentObservedMode::acquire);
    require (acquiring.mode == AgentObservedMode::acquire,
             "Observe must update the mode");
    require (acquiring.revision == 1,
             "A mode change must increment the revision");

    const auto unchanged = store.observe (AgentObservedMode::acquire);
    require (unchanged.revision == 1,
             "Observing the same mode must preserve the revision");

    const auto recording = store.observe (AgentObservedMode::record);
    require (recording.mode == AgentObservedMode::record,
             "A later mode change must be observed");
    require (recording.revision == 2,
             "Each distinct mode change must increment the revision once");

    std::cout << "PASS AgentStateTests\n";
    return 0;
}
