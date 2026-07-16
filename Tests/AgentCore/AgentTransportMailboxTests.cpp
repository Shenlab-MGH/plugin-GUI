#include "../../Source/Agent/AgentTransportMailbox.h"

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
    AgentTransportMailbox mailbox;

    const AgentTransportRequest first {
        "mailbox-1",
        AgentObservedMode::acquire,
        4
    };
    const AgentTransportRequest second {
        "mailbox-2",
        AgentObservedMode::record,
        4
    };

    require (mailbox.submit (first)
                 == AgentMailboxSubmitOutcome::accepted,
             "The first request must be accepted");
    require (mailbox.submit (first)
                 == AgentMailboxSubmitOutcome::duplicate,
             "The same request ID and payload must be idempotent");
    require (mailbox.submit ({
                 "mailbox-1",
                 AgentObservedMode::record,
                 4
             }) == AgentMailboxSubmitOutcome::idConflict,
             "The same request ID with a different payload must be rejected");
    require (mailbox.submit (second)
                 == AgentMailboxSubmitOutcome::busy,
             "Only one mutation may be pending at a time");

    const auto pending = mailbox.takePending();
    require (pending.has_value()
                 && pending->requestId == "mailbox-1",
             "The message thread must receive the accepted request");
    require (! mailbox.takePending().has_value(),
             "A request may be taken only once");

    AgentTransportApplyResult completed {
        AgentTransportApplyOutcome::completed,
        {
            AgentTransportPlanOutcome::proposalReady,
            "mailbox-1",
            AgentObservedMode::idle,
            AgentObservedMode::acquire,
            4,
            {}
        },
        { "1.0.2-agent", AgentObservedMode::acquire, 5 }
    };
    require (mailbox.complete ("mailbox-1", completed),
             "The active request must accept one result");
    require (! mailbox.complete ("mailbox-1", completed),
             "A completed request must not be completed twice");

    const auto result = mailbox.resultFor ("mailbox-1");
    require (result.has_value()
                 && result->outcome
                        == AgentTransportApplyOutcome::completed,
             "Callers must be able to query the completed result");
    require (mailbox.submit (second)
                 == AgentMailboxSubmitOutcome::accepted,
             "A new request may start after completion");
    require (mailbox.lookup ("mailbox-2").state
                 == AgentMailboxRequestState::pending,
             "Lookup must distinguish a pending request");
    require (mailbox.takePending().has_value(),
             "The second request must become active");
    require (mailbox.lookup ("mailbox-2").state
                 == AgentMailboxRequestState::active,
             "Lookup must distinguish an active request");
    require (mailbox.complete ("mailbox-2", completed),
             "The second request must complete");

    AgentTransportMailbox evictionMailbox;
    for (int index = 0; index < 129; ++index)
    {
        AgentTransportRequest request {
            "eviction-" + std::to_string (index),
            AgentObservedMode::acquire,
            static_cast<std::uint64_t> (index)
        };
        require (evictionMailbox.submit (request)
                     == AgentMailboxSubmitOutcome::accepted,
                 "Eviction setup requests must be accepted");
        require (evictionMailbox.takePending().has_value(),
                 "Eviction setup requests must become active");
        require (evictionMailbox.complete (request.requestId, completed),
                 "Eviction setup requests must complete");
    }

    const AgentTransportRequest evicted {
        "eviction-0",
        AgentObservedMode::acquire,
        0
    };
    require (evictionMailbox.lookup (evicted.requestId).state
                 == AgentMailboxRequestState::expired,
             "Evicted results must remain identifiable as expired");
    require (evictionMailbox.submit (evicted)
                 == AgentMailboxSubmitOutcome::duplicate,
             "An expired request ID must never execute again");
    require (evictionMailbox.submit ({
                 evicted.requestId,
                 AgentObservedMode::record,
                 0
             }) == AgentMailboxSubmitOutcome::idConflict,
             "An expired request ID must retain its payload identity");

    AgentTransportMailbox shutdownMailbox;
    require (shutdownMailbox.submit (first)
                 == AgentMailboxSubmitOutcome::accepted,
             "Shutdown setup request must be accepted");
    shutdownMailbox.shutdown();
    const auto shutdownResult =
        shutdownMailbox.lookup (first.requestId);
    require (shutdownResult.state
                 == AgentMailboxRequestState::cancelled,
             "Shutdown must cancel a pending request");
    require (shutdownResult.terminalReason
                 == AgentMailboxTerminalReason::shutdown,
             "Shutdown cancellation must retain its terminal reason");
    require (shutdownMailbox.submit (second)
                 == AgentMailboxSubmitOutcome::shuttingDown,
             "Shutdown must reject new requests");
    require (shutdownMailbox.lookup ("never-seen").state
                 == AgentMailboxRequestState::unknown,
             "Lookup must distinguish an unknown request");

    AgentTransportMailbox activeShutdownMailbox;
    require (activeShutdownMailbox.submit (first)
                 == AgentMailboxSubmitOutcome::accepted,
             "Active shutdown setup request must be accepted");
    require (activeShutdownMailbox.takePending().has_value(),
             "Active shutdown setup request must become active");
    activeShutdownMailbox.shutdown();
    require (activeShutdownMailbox.lookup (first.requestId).state
                 == AgentMailboxRequestState::active,
             "Shutdown must allow an already executing request to finish");
    require (activeShutdownMailbox.complete (
                 first.requestId,
                 completed),
             "An active request must publish its result after shutdown");

    AgentTransportMailbox concurrentMailbox;
    AgentMailboxSubmitOutcome outcomeA =
        AgentMailboxSubmitOutcome::invalidRequest;
    AgentMailboxSubmitOutcome outcomeB =
        AgentMailboxSubmitOutcome::invalidRequest;

    std::thread a ([&]
    {
        outcomeA = concurrentMailbox.submit (first);
    });
    std::thread b ([&]
    {
        outcomeB = concurrentMailbox.submit (second);
    });
    a.join();
    b.join();

    const int acceptedCount =
        (outcomeA == AgentMailboxSubmitOutcome::accepted ? 1 : 0)
        + (outcomeB == AgentMailboxSubmitOutcome::accepted ? 1 : 0);
    const int busyCount =
        (outcomeA == AgentMailboxSubmitOutcome::busy ? 1 : 0)
        + (outcomeB == AgentMailboxSubmitOutcome::busy ? 1 : 0);
    require (acceptedCount == 1 && busyCount == 1,
             "Concurrent submissions must serialize to accepted plus busy");

    require (mailbox.submit ({
                 "",
                 AgentObservedMode::idle,
                 0
             }) == AgentMailboxSubmitOutcome::invalidRequest,
             "An empty audit request ID must be rejected");

    std::cout << "PASS AgentTransportMailboxTests\n";
    return 0;
}
