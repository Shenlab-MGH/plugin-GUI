#include "../../Source/Agent/RuntimeOptions.h"

#include <cstdlib>
#include <iostream>
#include <string>
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
}

int main()
{
    const auto defaults = RuntimeOptions::parse ({});
    require (defaults.ok(), "Default runtime options must parse");
    require (! defaults.options.headless,
             "GUI mode must remain the default");
    require (! defaults.options.disableNativeHttp,
             "Native HTTP must retain official default behavior");
    require (! defaults.options.disableUserPlugins,
             "User plugins must retain official default behavior");
    require (! defaults.options.enableAgentUiaReadOnly,
             "Agent UIA must remain disabled by default");
    require (! defaults.options.enableAgentUiaInteractive,
             "Interactive Agent UIA must remain disabled by default");
    require (! defaults.options.enableAgentMutation,
             "Agent mutation must remain disabled by default");
    require (defaults.options.agentPort == 37498,
             "Agent loopback must retain its default port");
    require (! defaults.options.agentPortExplicit,
             "Default Agent port must not be treated as required");
    require (defaults.options.stateDirectory.empty(),
             "Default state storage must remain unchanged");

    const auto isolated = RuntimeOptions::parse ({
        "--headless",
        "--state-dir",
        "D:\\isolated\\oe",
        "--no-http",
        "--agent-port",
        "38498",
        "--no-user-plugins",
        "D:\\configs\\source-sim.xml"
    });
    require (isolated.ok(), "A complete isolation profile must parse");
    require (isolated.options.headless,
             "Headless mode must be preserved");
    require (isolated.options.disableNativeHttp,
             "Native HTTP must be explicitly disabled");
    require (isolated.options.disableUserPlugins,
             "User plugin discovery must be explicitly disabled");
    require (isolated.options.agentPort == 38498,
             "The requested Agent port must be preserved");
    require (isolated.options.agentPortExplicit,
             "An explicit Agent port must be marked as required");
    require (isolated.options.stateDirectory == "D:\\isolated\\oe",
             "The state directory must be preserved");
    require (isolated.options.configurationFile
                 == "D:\\configs\\source-sim.xml",
             "The configuration file must be preserved");

    const auto uia = RuntimeOptions::parse ({
        "--agent-uia-readonly"
    });
    require (uia.ok(), "The read-only UIA option must parse");
    require (uia.options.enableAgentUiaReadOnly,
             "The read-only UIA option must be preserved");

    const auto interactiveUia = RuntimeOptions::parse ({
        "--agent-uia-interactive"
    });
    require (interactiveUia.ok(),
             "The interactive UIA option must parse");
    require (interactiveUia.options.enableAgentUiaInteractive,
             "The interactive UIA option must be preserved");

    const auto mutation = RuntimeOptions::parse ({
        "--agent-mutation"
    });
    require (mutation.ok() && mutation.options.enableAgentMutation,
             "Explicit Agent mutation authorization must parse");

    require (! RuntimeOptions::parse ({ "--state-dir" }).ok(),
             "A missing state directory must fail closed");
    require (! RuntimeOptions::parse ({
                   "--agent-port",
                   "not-a-port"
               }).ok(),
             "A non-numeric Agent port must fail closed");
    require (! RuntimeOptions::parse ({
                   "--agent-port",
                   "37497"
               }).ok(),
             "The native HTTP port must not be reused by Agent");
    require (! RuntimeOptions::parse ({
                   "--agent-port",
                   "70000"
               }).ok(),
             "An out-of-range Agent port must fail closed");
    require (! RuntimeOptions::parse ({
                   "--no-http",
                   "--no-http"
               }).ok(),
             "Duplicate isolation switches must fail closed");
    require (! RuntimeOptions::parse ({
                   "--agent-uia-readonly",
                   "--agent-uia-readonly"
               }).ok(),
             "Duplicate UIA switches must fail closed");
    require (! RuntimeOptions::parse ({
                   "--agent-uia-interactive",
                   "--agent-uia-interactive"
               }).ok(),
             "Duplicate interactive UIA switches must fail closed");
    require (! RuntimeOptions::parse ({
                   "--agent-uia-readonly",
                   "--agent-uia-interactive"
               }).ok(),
             "Read-only and interactive UIA must be mutually exclusive");
    require (! RuntimeOptions::parse ({
                   "--agent-uia-interactive",
                   "--agent-uia-readonly"
               }).ok(),
             "Interactive and read-only UIA must be mutually exclusive");
    require (! RuntimeOptions::parse ({
                   "--agent-mutation",
                   "--agent-mutation"
               }).ok(),
             "Duplicate mutation switches must fail closed");
    require (! RuntimeOptions::parse ({
                   "--unknown-isolation-switch"
               }).ok(),
             "Unknown options must fail closed");
    require (! RuntimeOptions::parse ({
                   "first.xml",
                   "second.xml"
               }).ok(),
             "More than one configuration file must fail closed");

    std::cout << "PASS RuntimeOptionsTests\n";
    return 0;
}
