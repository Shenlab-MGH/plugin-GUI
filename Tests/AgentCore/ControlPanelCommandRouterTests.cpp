#include "../../Source/Agent/ControlPanelCommandRouter.h"

#include <cstdlib>
#include <iostream>
#include <optional>

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

class RecordingDispatcher final : public AgentCommandDispatcher
{
public:
    void dispatch (const AgentCommand& command) override
    {
        lastCommand = command;
    }

    std::optional<AgentCommand> lastCommand;
};
}

int main()
{
    RecordingDispatcher dispatcher;
    ControlPanelCommandRouter router (dispatcher);

    router.requestAcquisitionToggle (AgentCommandOrigin::userInterface);
    require (dispatcher.lastCommand.has_value(),
             "Acquisition request must dispatch a command");
    require (dispatcher.lastCommand->type
                 == AgentCommandType::requestAcquisitionToggle,
             "Acquisition request must use the acquisition command type");
    require (dispatcher.lastCommand->origin
                 == AgentCommandOrigin::userInterface,
             "Acquisition request must preserve user-interface origin");

    router.requestRecordingToggle (AgentCommandOrigin::accessibility);
    require (dispatcher.lastCommand->type
                 == AgentCommandType::requestRecordingToggle,
             "Recording request must use the recording command type");
    require (dispatcher.lastCommand->origin
                 == AgentCommandOrigin::accessibility,
             "Recording request must preserve accessibility origin");

    std::cout << "PASS ControlPanelCommandRouterTests\n";
    return 0;
}
