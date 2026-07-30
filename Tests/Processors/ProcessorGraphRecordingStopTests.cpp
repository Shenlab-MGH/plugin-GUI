#include "../../Source/Processors/ProcessorGraph/ProcessorGraphRecordingStop.h"

#include "gtest/gtest.h"

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;
using Error = RecordNodeRecordingStopError;
using NodeRequest = ProcessorGraphRecordingStopNodeRequest;
using RequestBatch = ProcessorGraphRecordingStopRequestBatch;
using NodeResult = ProcessorGraphRecordingStopNodeResult;
using Result = ProcessorGraphRecordingStopResult;
using Status = ProcessorGraphRecordingStopStatus;
using RequestResult = RecordNodeRecordingStopRequestResult;
using Completion = RecordNodeRecordingStopResult;

RequestResult requestAvailable (int nodeId)
{
    return { nodeId, true, std::nullopt };
}

RequestResult requestUnavailable (int nodeId)
{
    return { nodeId, false, Error::writerUnavailable };
}

Completion unavailableCompletion (int nodeId)
{
    return { nodeId, false, Error::writerUnavailable };
}

Completion clean (int nodeId)
{
    return { nodeId, false, std::nullopt };
}

Completion unclean (int nodeId)
{
    return { nodeId, false, Error::writerExitedUncleanly };
}

Completion timedOut (int nodeId)
{
    return { nodeId, true, Error::writerTimedOut };
}

Completion unknownFailure (int nodeId)
{
    return { nodeId, false, static_cast<Error> (999) };
}

struct NodePlan
{
    RequestResult request;
    Completion completion;
};

struct FakeOwner
{
    std::vector<NodePlan> plans;
    int recordNodeCountCalls = 0;
    std::vector<std::size_t> requestedIndexes;
    std::vector<std::size_t> waitedIndexes;
    std::vector<RequestResult> waitedTokens;
    std::vector<Clock::time_point> waitedDeadlines;
    std::vector<std::string> trace;

    std::size_t recordNodeCount()
    {
        ++recordNodeCountCalls;
        trace.push_back ("count");
        return plans.size();
    }

    RequestResult requestRecordingStopAt (std::size_t index)
    {
        requestedIndexes.push_back (index);
        trace.push_back ("request:" + std::to_string (index));
        return plans.at (index).request;
    }

    Completion waitForRecordingStopUntilAt (
        std::size_t index,
        const RequestResult& token,
        Clock::time_point deadline)
    {
        waitedIndexes.push_back (index);
        waitedTokens.push_back (token);
        waitedDeadlines.push_back (deadline);
        trace.push_back ("wait:" + std::to_string (index));
        return plans.at (index).completion;
    }
};

FakeOwner ownerWith (std::vector<NodePlan> plans)
{
    return { std::move (plans) };
}

void expectNode (
    const NodeResult& actual,
    std::size_t expectedIndex,
    const RequestResult& expectedRequest,
    const Completion& expectedCompletion)
{
    EXPECT_EQ (actual.graphIndex, expectedIndex);
    EXPECT_EQ (actual.request.nodeId, expectedRequest.nodeId);
    EXPECT_EQ (actual.request.writerAvailable,
               expectedRequest.writerAvailable);
    EXPECT_EQ (actual.request.error, expectedRequest.error);
    EXPECT_EQ (actual.completion.nodeId, expectedCompletion.nodeId);
    EXPECT_EQ (actual.completion.writerThreadRunning,
               expectedCompletion.writerThreadRunning);
    EXPECT_EQ (actual.completion.error, expectedCompletion.error);
}

} // namespace

TEST (ProcessorGraphRecordingStopTests, ZeroNodesStopsCleanlyWithoutWaiting)
{
    auto owner = ownerWith ({ });
    const auto deadline = Clock::time_point {} + std::chrono::seconds (7);

    const auto result =
        stopProcessorGraphRecordingUntil (owner, deadline);

    EXPECT_EQ (result.status, Status::stoppedCleanly);
    EXPECT_FALSE (result.error.has_value());
    EXPECT_TRUE (result.nodes.empty());
    EXPECT_EQ (owner.recordNodeCountCalls, 1);
    EXPECT_TRUE (owner.requestedIndexes.empty());
    EXPECT_TRUE (owner.waitedIndexes.empty());
}

TEST (ProcessorGraphRecordingStopTests,
      RequestUsesStableGraphIndexesAndPreservesNonmonotonicTokens)
{
    const auto first = requestAvailable (90);
    const auto second = requestUnavailable (-4);
    const auto third = requestAvailable (18);
    auto owner = ownerWith ({ { first, clean (90) },
                               { second, unavailableCompletion (-4) },
                               { third, clean (18) } });

    const auto batch = requestProcessorGraphRecordingStop (owner);

    ASSERT_EQ (batch.nodes.size(), 3u);
    EXPECT_EQ (owner.recordNodeCountCalls, 1);
    EXPECT_EQ (owner.requestedIndexes,
               (std::vector<std::size_t> { 0, 1, 2 }));
    EXPECT_TRUE (owner.waitedIndexes.empty());
    EXPECT_EQ (batch.nodes[0].graphIndex, 0u);
    EXPECT_EQ (batch.nodes[1].graphIndex, 1u);
    EXPECT_EQ (batch.nodes[2].graphIndex, 2u);
    EXPECT_EQ (batch.nodes[0].request.nodeId, 90);
    EXPECT_EQ (batch.nodes[1].request.nodeId, -4);
    EXPECT_EQ (batch.nodes[2].request.nodeId, 18);
}

TEST (ProcessorGraphRecordingStopTests,
      ComposeCompletesEveryRequestBeforeBeginningAnyWait)
{
    auto owner = ownerWith ({ { requestUnavailable (3),
                                 unavailableCompletion (3) },
                               { requestAvailable (8), unclean (8) },
                               { requestAvailable (2), timedOut (2) } });

    const auto result = stopProcessorGraphRecordingUntil (
        owner,
        Clock::time_point {} + std::chrono::seconds (1));

    EXPECT_EQ (result.status, Status::timedOut);
    EXPECT_EQ (owner.trace,
               (std::vector<std::string> {
                   "count", "request:0", "request:1", "request:2",
                   "wait:0", "wait:1", "wait:2"
               }));
}

TEST (ProcessorGraphRecordingStopTests,
      AllCleanNodesProduceCleanAggregateAndRetainEveryDetail)
{
    const auto first = requestAvailable (1);
    const auto second = requestAvailable (2);
    auto owner = ownerWith ({ { first, clean (1) },
                               { second, clean (2) } });
    const auto deadline = Clock::time_point {} + std::chrono::seconds (4);

    const auto batch = requestProcessorGraphRecordingStop (owner);
    const auto result =
        waitForProcessorGraphRecordingStopUntil (batch, owner, deadline);

    EXPECT_EQ (result.status, Status::stoppedCleanly);
    EXPECT_FALSE (result.error.has_value());
    ASSERT_EQ (result.nodes.size(), 2u);
    expectNode (result.nodes[0], 0, first, clean (1));
    expectNode (result.nodes[1], 1, second, clean (2));
}

TEST (ProcessorGraphRecordingStopTests,
      UncleanNodeCannotReportCleanStop)
{
    const auto token = requestAvailable (42);
    const auto completion = unclean (42);
    auto owner = ownerWith ({ { token, completion } });

    const auto result = stopProcessorGraphRecordingUntil (
        owner,
        Clock::time_point {} + std::chrono::seconds (3));

    EXPECT_EQ (result.status, Status::stoppedWithFailure);
    ASSERT_TRUE (result.error.has_value());
    EXPECT_EQ (*result.error, Error::writerExitedUncleanly);
    ASSERT_EQ (result.nodes.size(), 1u);
    expectNode (result.nodes[0], 0, token, completion);
}

TEST (ProcessorGraphRecordingStopTests,
      MixedOutcomesRetainEveryNodeAndPrioritizeTimeout)
{
    const auto unavailable = requestUnavailable (17);
    const auto uncleanToken = requestAvailable (32);
    const auto timeoutToken = requestAvailable (91);
    auto owner = ownerWith ({ { unavailable, unavailableCompletion (17) },
                               { uncleanToken, unclean (32) },
                               { timeoutToken, timedOut (91) } });

    const auto result = stopProcessorGraphRecordingUntil (
        owner,
        Clock::time_point {} + std::chrono::seconds (8));

    EXPECT_EQ (result.status, Status::timedOut);
    ASSERT_TRUE (result.error.has_value());
    EXPECT_EQ (*result.error, Error::writerTimedOut);
    ASSERT_EQ (result.nodes.size(), 3u);
    expectNode (result.nodes[0], 0, unavailable, unavailableCompletion (17));
    expectNode (result.nodes[1], 1, uncleanToken, unclean (32));
    expectNode (result.nodes[2], 2, timeoutToken, timedOut (91));
    EXPECT_EQ (owner.requestedIndexes,
               (std::vector<std::size_t> { 0, 1, 2 }));
    EXPECT_EQ (owner.waitedIndexes,
               (std::vector<std::size_t> { 0, 1, 2 }));
}

TEST (ProcessorGraphRecordingStopTests,
      AggregateFailurePriorityDoesNotDependOnGraphOrder)
{
    const auto unavailable = requestUnavailable (4);
    const auto uncleanToken = requestAvailable (5);
    const auto timeoutToken = requestAvailable (6);
    const auto deadline = Clock::time_point {} + std::chrono::seconds (2);

    auto unavailableFirst = ownerWith ({ { unavailable, unavailableCompletion (4) },
                                         { uncleanToken, unclean (5) } });
    auto uncleanFirst = ownerWith ({ { uncleanToken, unclean (5) },
                                     { unavailable, unavailableCompletion (4) } });
    auto timeoutLast = ownerWith ({ { unavailable, unavailableCompletion (4) },
                                    { uncleanToken, unclean (5) },
                                    { timeoutToken, timedOut (6) } });
    auto timeoutFirst = ownerWith ({ { timeoutToken, timedOut (6) },
                                     { uncleanToken, unclean (5) },
                                     { unavailable, unavailableCompletion (4) } });

    const auto first =
        stopProcessorGraphRecordingUntil (unavailableFirst, deadline);
    const auto second =
        stopProcessorGraphRecordingUntil (uncleanFirst, deadline);
    const auto third =
        stopProcessorGraphRecordingUntil (timeoutLast, deadline);
    const auto fourth =
        stopProcessorGraphRecordingUntil (timeoutFirst, deadline);

    EXPECT_EQ (first.status, Status::stoppedWithFailure);
    EXPECT_EQ (first.error, Error::writerUnavailable);
    EXPECT_EQ (second.status, Status::stoppedWithFailure);
    EXPECT_EQ (second.error, Error::writerUnavailable);
    EXPECT_EQ (third.status, Status::timedOut);
    EXPECT_EQ (third.error, Error::writerTimedOut);
    EXPECT_EQ (fourth.status, Status::timedOut);
    EXPECT_EQ (fourth.error, Error::writerTimedOut);
}

TEST (ProcessorGraphRecordingStopTests,
      EveryWaitReceivesTheSameAbsoluteDeadline)
{
    auto owner = ownerWith ({ { requestAvailable (11), clean (11) },
                               { requestAvailable (12), clean (12) },
                               { requestAvailable (13), clean (13) } });
    const auto deadline = Clock::time_point {} +
        std::chrono::microseconds (1234567);

    const auto result = stopProcessorGraphRecordingUntil (owner, deadline);

    EXPECT_EQ (result.status, Status::stoppedCleanly);
    ASSERT_EQ (owner.waitedDeadlines.size(), 3u);
    EXPECT_EQ (owner.waitedDeadlines[0], deadline);
    EXPECT_EQ (owner.waitedDeadlines[1], deadline);
    EXPECT_EQ (owner.waitedDeadlines[2], deadline);
}

TEST (ProcessorGraphRecordingStopTests,
      SavedRequestBatchCanRetryWaitAfterTimeoutWithALaterDeadline)
{
    const auto token = requestAvailable (55);
    auto owner = ownerWith ({ { token, timedOut (55) } });
    const auto firstDeadline = Clock::time_point {} + std::chrono::seconds (5);
    const auto secondDeadline = Clock::time_point {} + std::chrono::seconds (12);

    const auto batch = requestProcessorGraphRecordingStop (owner);
    const auto first = waitForProcessorGraphRecordingStopUntil (
        batch,
        owner,
        firstDeadline);
    owner.plans[0].completion = clean (55);
    const auto second = waitForProcessorGraphRecordingStopUntil (
        batch,
        owner,
        secondDeadline);

    EXPECT_EQ (first.status, Status::timedOut);
    EXPECT_EQ (first.error, Error::writerTimedOut);
    EXPECT_EQ (second.status, Status::stoppedCleanly);
    EXPECT_FALSE (second.error.has_value());
    EXPECT_EQ (owner.recordNodeCountCalls, 1);
    EXPECT_EQ (owner.requestedIndexes,
               (std::vector<std::size_t> { 0 }));
    EXPECT_EQ (owner.waitedIndexes,
               (std::vector<std::size_t> { 0, 0 }));
    ASSERT_EQ (owner.waitedTokens.size(), 2u);
    EXPECT_EQ (owner.waitedTokens[0].nodeId, 55);
    EXPECT_EQ (owner.waitedTokens[1].nodeId, 55);
    ASSERT_EQ (owner.waitedDeadlines.size(), 2u);
    EXPECT_EQ (owner.waitedDeadlines[0], firstDeadline);
    EXPECT_EQ (owner.waitedDeadlines[1], secondDeadline);
}

TEST (ProcessorGraphRecordingStopTests,
      WaitUsesSavedBatchWithoutReenumeratingOrRequestingAgain)
{
    const auto first = requestAvailable (70);
    const auto second = requestAvailable (71);
    auto owner = ownerWith ({ { first, clean (70) },
                               { second, clean (71) } });
    const auto batch = requestProcessorGraphRecordingStop (owner);

    owner.plans.clear();
    owner.plans = { { first, clean (70) }, { second, clean (71) } };
    const auto result = waitForProcessorGraphRecordingStopUntil (
        batch,
        owner,
        Clock::time_point {} + std::chrono::seconds (6));

    EXPECT_EQ (result.status, Status::stoppedCleanly);
    EXPECT_EQ (owner.recordNodeCountCalls, 1);
    EXPECT_EQ (owner.requestedIndexes,
               (std::vector<std::size_t> { 0, 1 }));
    EXPECT_EQ (owner.waitedIndexes,
               (std::vector<std::size_t> { 0, 1 }));
    ASSERT_EQ (owner.waitedTokens.size(), 2u);
    EXPECT_EQ (owner.waitedTokens[0].nodeId, 70);
    EXPECT_EQ (owner.waitedTokens[1].nodeId, 71);
}

TEST (ProcessorGraphRecordingStopTests,
      UnavailableRequestDoesNotBlockLaterRequestsOrWaits)
{
    const auto unavailable = requestUnavailable (100);
    const auto available = requestAvailable (101);
    auto owner = ownerWith ({ { unavailable, unavailableCompletion (100) },
                               { available, clean (101) } });

    const auto result = stopProcessorGraphRecordingUntil (
        owner,
        Clock::time_point {} + std::chrono::seconds (3));

    EXPECT_EQ (result.status, Status::stoppedWithFailure);
    ASSERT_TRUE (result.error.has_value());
    EXPECT_EQ (*result.error, Error::writerUnavailable);
    EXPECT_EQ (owner.requestedIndexes,
               (std::vector<std::size_t> { 0, 1 }));
    EXPECT_EQ (owner.waitedIndexes,
               (std::vector<std::size_t> { 0, 1 }));
    ASSERT_EQ (result.nodes.size(), 2u);
    expectNode (result.nodes[0], 0, unavailable, unavailableCompletion (100));
    expectNode (result.nodes[1], 1, available, clean (101));
}

TEST (ProcessorGraphRecordingStopTests,
      FailedWaitDoesNotBlockLaterWaits)
{
    const auto first = requestAvailable (201);
    const auto second = requestAvailable (202);
    const auto third = requestAvailable (203);
    auto owner = ownerWith ({ { first, timedOut (201) },
                               { second, unclean (202) },
                               { third, clean (203) } });

    const auto result = stopProcessorGraphRecordingUntil (
        owner,
        Clock::time_point {} + std::chrono::seconds (9));

    EXPECT_EQ (result.status, Status::timedOut);
    ASSERT_TRUE (result.error.has_value());
    EXPECT_EQ (*result.error, Error::writerTimedOut);
    EXPECT_EQ (owner.waitedIndexes,
               (std::vector<std::size_t> { 0, 1, 2 }));
    ASSERT_EQ (result.nodes.size(), 3u);
    expectNode (result.nodes[0], 0, first, timedOut (201));
    expectNode (result.nodes[1], 1, second, unclean (202));
    expectNode (result.nodes[2], 2, third, clean (203));
}

TEST (ProcessorGraphRecordingStopTests,
      UnknownFutureNodeErrorIsConservativelyReportedAndRetained)
{
    const auto token = requestAvailable (260);
    const auto unknown = unknownFailure (260);
    const auto deadline = Clock::time_point {} + std::chrono::seconds (9);
    auto unknownOwner = ownerWith ({ { token, unknown } });
    auto timeoutOwner = ownerWith ({ { token, unknown },
                                     { requestAvailable (261), timedOut (261) } });

    const auto unknownResult =
        stopProcessorGraphRecordingUntil (unknownOwner, deadline);
    const auto timeoutResult =
        stopProcessorGraphRecordingUntil (timeoutOwner, deadline);

    EXPECT_EQ (unknownResult.status, Status::stoppedWithFailure);
    ASSERT_TRUE (unknownResult.error.has_value());
    EXPECT_EQ (*unknownResult.error, static_cast<Error> (999));
    ASSERT_EQ (unknownResult.nodes.size(), 1u);
    expectNode (unknownResult.nodes[0], 0, token, unknown);
    EXPECT_EQ (timeoutResult.status, Status::timedOut);
    EXPECT_EQ (timeoutResult.error, Error::writerTimedOut);
}

TEST (ProcessorGraphRecordingStopTests,
      WholeComposeCanRetryAfterTimeoutWithALaterDeadline)
{
    const auto token = requestAvailable (303);
    auto owner = ownerWith ({ { token, timedOut (303) } });
    const auto firstDeadline = Clock::time_point {} + std::chrono::seconds (5);
    const auto secondDeadline = Clock::time_point {} + std::chrono::seconds (12);

    const auto first =
        stopProcessorGraphRecordingUntil (owner, firstDeadline);
    owner.plans[0].completion = clean (303);
    const auto second =
        stopProcessorGraphRecordingUntil (owner, secondDeadline);

    EXPECT_EQ (first.status, Status::timedOut);
    EXPECT_EQ (first.error, Error::writerTimedOut);
    EXPECT_EQ (second.status, Status::stoppedCleanly);
    EXPECT_FALSE (second.error.has_value());
    EXPECT_EQ (owner.recordNodeCountCalls, 2);
    EXPECT_EQ (owner.requestedIndexes,
               (std::vector<std::size_t> { 0, 0 }));
    EXPECT_EQ (owner.waitedIndexes,
               (std::vector<std::size_t> { 0, 0 }));
    ASSERT_EQ (owner.waitedDeadlines.size(), 2u);
    EXPECT_EQ (owner.waitedDeadlines[0], firstDeadline);
    EXPECT_EQ (owner.waitedDeadlines[1], secondDeadline);
    EXPECT_EQ (owner.trace,
               (std::vector<std::string> {
                   "count", "request:0", "wait:0",
                   "count", "request:0", "wait:0"
               }));
}
