#include "../../Source/Agent/AgentExperimentDirectoryEndpoint.h"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <utility>
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

class FakeExecutor final : public AgentExperimentDirectoryExecutor
{
public:
    AgentDirectoryApplyResult apply (
        const AgentDirectoryRequest& request) override
    {
        ++applyCount;
        lastSnapshot = {
            request.approvedRoot,
            request.directoryName,
            request.approvedRoot + "\\" + request.directoryName,
            true,
            false,
            AgentObservedMode::idle,
            request.expectedRevision + 1
        };
        return {
            { AgentDirectoryOutcome::ready, lastSnapshot.targetPath },
            lastSnapshot
        };
    }

    AgentRecordingDirectorySnapshot snapshot() override
    {
        return lastSnapshot;
    }

    int applyCount = 0;
    AgentRecordingDirectorySnapshot lastSnapshot;
};
}

int main()
{
    std::vector<std::function<void()>> scheduled;
    auto endpoint = AgentExperimentDirectoryEndpoint::create (
        [&scheduled] (std::function<void()> callback)
        {
            scheduled.push_back (std::move (callback));
            return true;
        },
        {});
    auto executor = std::make_shared<FakeExecutor>();
    endpoint->attachExecutor (executor);
    endpoint->publish ({
        "D:\\recordings", {}, {}, false, false,
        AgentObservedMode::idle, 7
    });
    require (endpoint->snapshot().revision == 7,
             "Authoritative GUI observations must be publishable");

    const AgentDirectoryEndpointRequest request {
        "run-1",
        "dir-1",
        { "D:\\recordings", "mouseA_shank_01", 7 }
    };
    require (endpoint->submit (request)
                 == AgentDirectorySubmitOutcome::accepted,
             "A ready endpoint must accept a directory request");
    require (executor->applyCount == 0,
             "The executor must not run on the submitting thread");
    require (scheduled.size() == 1,
             "An accepted request must schedule one callback");
    require (endpoint->query (request.commandId).state
                 == AgentDirectoryRequestState::pending,
             "An accepted request must be visible as pending");

    scheduled.front()();
    require (executor->applyCount == 1,
             "The executor must run exactly once on callback drain");
    const auto completed = endpoint->query (request.commandId);
    require (completed.state == AgentDirectoryRequestState::completed,
             "A drained request must be completed");
    require (completed.result.has_value()
                 && completed.result->snapshot.prepared,
             "Completion must include prepared readback");
    require (endpoint->snapshot().directoryName
                 == "mouseA_shank_01",
             "Endpoint snapshot must publish authoritative readback");

    require (endpoint->submit (request)
                 == AgentDirectorySubmitOutcome::duplicate,
             "An identical request id and payload must be idempotent");
    auto conflict = request;
    conflict.request.directoryName = "mouseA_shank_02";
    require (endpoint->submit (conflict)
                 == AgentDirectorySubmitOutcome::idConflict,
             "A reused request id with changed payload must conflict");

    endpoint->detachExecutor (executor);
    require (endpoint->submit ({
                 "run-1", "dir-2",
                 { "D:\\recordings", "mouseA_shank_02", 8 }
             }) == AgentDirectorySubmitOutcome::unavailable,
             "A detached endpoint must reject mutation");

    endpoint->attachExecutor (executor);
    endpoint->beginShutdown();
    require (endpoint->submit ({
                 "run-1", "dir-3",
                 { "D:\\recordings", "mouseA_shank_02", 8 }
             }) == AgentDirectorySubmitOutcome::shuttingDown,
             "A stopped endpoint must reject mutation");

    std::cout << "PASS AgentExperimentDirectoryEndpointTests\n";
    return 0;
}
