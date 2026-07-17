#include "../../Source/Agent/AgentRecordingWatchdog.h"

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
    const AgentWatchdogPlan plan {
        120000,
        3000,
        10000,
        250,
        true
    };
    AgentRecordingWatchdog watchdog (plan);

    require (watchdog.tick ({
                 60000, AgentObservedMode::record, true
             }).decision == AgentWatchdogDecision::observe,
             "Active recording before deadline must be observed");
    require (watchdog.tick ({
                 120000, AgentObservedMode::record, true
             }).decision == AgentWatchdogDecision::requestVerifiedStop,
             "Proven active recording at deadline must request safe stop");
    require (watchdog.tick ({
                 120000, AgentObservedMode::unknown, true
             }).decision == AgentWatchdogDecision::requireManualTakeover,
             "Unknown state at deadline must never toggle");
    require (watchdog.pause ({
                 60000, AgentObservedMode::record, true
             }).recordingAction == AgentRecordingAction::none,
             "Pause must not stop active recording");

    AgentRecordingWatchdog noStop ({
        120000, 3000, 10000, 250, false
    });
    require (noStop.tick ({
                 120000, AgentObservedMode::record, true
             }).decision == AgentWatchdogDecision::requireManualTakeover,
             "Deadline without stop authority requires takeover");
    require (watchdog.tick ({
                 133001, AgentObservedMode::record, true
             }).decision == AgentWatchdogDecision::requireManualTakeover,
             "Maximum overrun must escalate instead of retrying forever");
    require (watchdog.tick ({
                 121000, AgentObservedMode::acquire, true
             }).decision == AgentWatchdogDecision::complete,
             "Verified stopped recording after target is complete");

    std::cout << "PASS AgentRecordingWatchdogTests\n";
    return 0;
}
