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
