#include "../../Source/Agent/AgentCommand.h"

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
    const AgentCommand command {
        AgentCommandType::requestRecordingToggle,
        AgentCommandOrigin::accessibility
    };

    require (command.type == AgentCommandType::requestRecordingToggle,
             "AgentCommand must preserve its command type");
    require (command.origin == AgentCommandOrigin::accessibility,
             "AgentCommand must preserve its command origin");

    std::cout << "PASS AgentCommandTests\n";
    return 0;
}
