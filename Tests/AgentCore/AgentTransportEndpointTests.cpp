#include "../../Source/Agent/AgentTransportEndpoint.h"

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

class FakeExecutor final : public AgentTransportExecutor
{
public:
    AgentTransportApplyResult apply (
        const AgentTransportRequest& request) override
    {
        ++applyCount;
        return {
            AgentTransportApplyOutcome::completed,
            {
                AgentTransportPlanOutcome::proposalReady,
                request.requestId,
                AgentObservedMode::idle,
                request.targetMode,
                request.expectedRevision,
                {}
            },
            {
                "1.0.2-agent",
                request.targetMode,
                request.expectedRevision + 1
            }
        };
    }

    int applyCount = 0;
};

class ThrowingExecutor final : public AgentTransportExecutor
{
public:
    AgentTransportApplyResult apply (
        const AgentTransportRequest&) override
    {
        throw 42;
    }
};
}

int main()
{
    std::vector<std::function<void()>> scheduled;
    auto endpoint = AgentTransportEndpoint::create (
        [&scheduled] (std::function<void()> callback)
        {
            scheduled.push_back (std::move (callback));
            return true;
        },
        { "1.0.2-agent", AgentObservedMode::idle, 1 });
    auto executor = std::make_shared<FakeExecutor>();
    endpoint->attachExecutor (executor);
    require (endpoint->serviceSnapshot().phase
                 == AgentEndpointPhase::ready,
             "Attaching an executor must publish READY service state");

    auto nullEndpoint = AgentTransportEndpoint::create (
        [] (std::function<void()>)
        {
            return true;
        },
        { "1.0.2-agent", AgentObservedMode::idle, 1 });
    nullEndpoint->attachExecutor (nullptr);
    require (nullEndpoint->serviceSnapshot().phase
                 == AgentEndpointPhase::detached,
             "A null executor must never publish READY service state");

    const AgentTransportRequest request {
        "endpoint-1",
        AgentObservedMode::acquire,
        1
    };
    require (endpoint->submit (request)
                 == AgentMailboxSubmitOutcome::accepted,
             "A ready endpoint must accept one request");
    require (endpoint->query (request.requestId).state
                 == AgentMailboxRequestState::pending,
             "An accepted request must be observable as pending");
    require (scheduled.size() == 1,
             "An accepted request must schedule one message-thread drain");

    endpoint->detachExecutor (executor);
    require (endpoint->serviceSnapshot().phase
                 == AgentEndpointPhase::detached,
             "Detaching an executor must publish DETACHED service state");
    executor.reset();
    scheduled.front()();
    const auto detachedResult = endpoint->query (request.requestId);
    require (detachedResult.state
                 == AgentMailboxRequestState::cancelled,
             "A queued callback after detach must terminalize the request");
    require (detachedResult.terminalReason
                 == AgentMailboxTerminalReason::executorDetached,
             "Detach cancellation must report executorDetached");

    auto secondExecutor = std::make_shared<FakeExecutor>();
    endpoint->attachExecutor (secondExecutor);
    const AgentTransportRequest second {
        "endpoint-2",
        AgentObservedMode::record,
        1
    };
    require (endpoint->submit (second)
                 == AgentMailboxSubmitOutcome::accepted,
             "A reattached endpoint must accept work");
    scheduled.back()();
    require (secondExecutor->applyCount == 1,
             "The executor must run exactly once");
    require (endpoint->query (second.requestId).state
                 == AgentMailboxRequestState::completed,
             "Successful execution must publish a completed result");

    auto unavailable = AgentTransportEndpoint::create (
        [] (std::function<void()>)
        {
            return false;
        },
        { "1.0.2-agent", AgentObservedMode::idle, 3 });
    unavailable->attachExecutor (secondExecutor);
    const AgentTransportRequest unscheduled {
        "endpoint-unscheduled",
        AgentObservedMode::acquire,
        3
    };
    require (unavailable->submit (unscheduled)
                 == AgentMailboxSubmitOutcome::unavailable,
             "Scheduler failure must be returned to the caller");
    const auto unavailableResult =
        unavailable->query (unscheduled.requestId);
    require (unavailableResult.state
                 == AgentMailboxRequestState::cancelled,
             "Scheduler failure must terminalize the accepted request");
    require (unavailableResult.terminalReason
                 == AgentMailboxTerminalReason::dispatchUnavailable,
             "Scheduler failure must report dispatchUnavailable");

    auto throwing = AgentTransportEndpoint::create (
        [&scheduled] (std::function<void()> callback)
        {
            scheduled.push_back (std::move (callback));
            return true;
        },
        { "1.0.2-agent", AgentObservedMode::idle, 3 });
    auto throwingExecutor =
        std::make_shared<ThrowingExecutor>();
    throwing->attachExecutor (throwingExecutor);
    const AgentTransportRequest exceptionRequest {
        "endpoint-exception",
        AgentObservedMode::acquire,
        3
    };
    require (throwing->submit (exceptionRequest)
                 == AgentMailboxSubmitOutcome::accepted,
             "A throwing executor request must initially be accepted");
    scheduled.back()();
    const auto exceptionResult =
        throwing->query (exceptionRequest.requestId);
    require (exceptionResult.state
                 == AgentMailboxRequestState::completed,
             "An executor exception must still reach a terminal result");
    require (exceptionResult.result.has_value()
                 && exceptionResult.result->outcome
                        == AgentTransportApplyOutcome::executionFailed,
             "An executor exception must be reported as executionFailed");

    endpoint->publish ({
        "1.0.2-agent",
        AgentObservedMode::record,
        2
    });
    endpoint->beginShutdown();
    require (endpoint->serviceSnapshot().phase
                 == AgentEndpointPhase::stopped,
             "Shutdown must publish STOPPED service state");
    require (endpoint->submit ({
                 "endpoint-after-shutdown",
                 AgentObservedMode::idle,
                 2
             }) == AgentMailboxSubmitOutcome::shuttingDown,
             "Shutdown must reject new work");
    require (endpoint->serviceSnapshot().transport.mode
                 == AgentObservedMode::record,
             "Stopped service state must retain last transport evidence");

    std::cout << "PASS AgentTransportEndpointTests\n";
    return 0;
}
