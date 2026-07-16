#include "../../Source/Agent/TransportAccessibilityRegistry.h"

#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

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
    TransportAccessibilityRegistry registry;
    const auto& controls = registry.controls();

    require (controls.size() == 2,
             "v0.0.1 accessibility allowlist must contain exactly two controls");

    std::set<std::string> ids;
    for (const auto& control : controls)
    {
        require (! control.automationId.empty(),
                 "Every allowlisted control must have an AutomationId");
        require (! control.name.empty(),
                 "Every allowlisted control must have a name");
        require (! control.description.empty(),
                 "Every allowlisted control must have a description");
        require (! control.helpText.empty(),
                 "Every allowlisted control must have help text");
        require (ids.insert (control.automationId).second,
                 "AutomationIds must be unique");
    }

    const auto* acquisition = registry.find (
        "oe.transport.acquisition");
    require (acquisition != nullptr,
             "Acquisition must be allowlisted");
    require (acquisition->schemaVersion == 1,
             "Accessibility descriptors must be versioned");
    require (acquisition->kind == TransportControlKind::acquisition,
             "Acquisition must have an explicit semantic kind");
    require (acquisition->requiredPattern
                 == TransportAccessibilityPattern::invoke,
             "Acquisition must require the UIA Invoke pattern");
    require (! acquisition->requiresRecordingPreflight,
             "Acquisition must not claim recording preflight");
    require (&registry.get (TransportControlKind::acquisition)
                 == acquisition,
             "Strongly typed acquisition lookup must return the allowlist entry");

    const auto* recording = registry.find ("oe.transport.recording");
    require (recording != nullptr,
             "Recording must be allowlisted");
    require (recording->kind == TransportControlKind::recording,
             "Recording must have an explicit semantic kind");
    require (recording->requiredPattern
                 == TransportAccessibilityPattern::invoke,
             "Recording must require the UIA Invoke pattern");
    require (recording->requiresRecordingPreflight,
             "Recording must require the recording safety preflight");
    require (&registry.get (TransportControlKind::recording)
                 == recording,
             "Strongly typed recording lookup must return the allowlist entry");

    require (registry.find ("oe.signal-chain.delete") == nullptr,
             "Non-transport controls must not enter the v0.0.1 allowlist");
    require (registry.find ("") == nullptr,
             "An empty AutomationId must never resolve");

    std::cout << "PASS TransportAccessibilityRegistryTests\n";
    return 0;
}
