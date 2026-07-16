#include "../../Source/Agent/AgentStateSnapshotCache.h"

#include <cstdlib>
#include <iostream>
#include <thread>

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
    AgentStateSnapshotCache cache ({
        "1.0.2-agent",
        AgentObservedMode::unknown,
        0
    });

    require (cache.snapshot().mode == AgentObservedMode::unknown,
             "The cache must preserve its initial snapshot");

    std::thread publisher ([&]
    {
        cache.publish ({
            "1.0.2-agent",
            AgentObservedMode::acquire,
            9
        });
    });
    publisher.join();

    AgentStateSnapshot observed;
    std::thread reader ([&]
    {
        observed = cache.snapshot();
    });
    reader.join();

    require (observed.mode == AgentObservedMode::acquire
                 && observed.revision == 9,
             "A worker thread must read a coherent published snapshot");
    require (observed.guiVersion == "1.0.2-agent",
             "The published snapshot must remain internally coherent");

    std::cout << "PASS AgentStateSnapshotCacheTests\n";
    return 0;
}
