#include "../../Source/Processors/RecordNode/RecordNodeRecordingStop.h"

#include "gtest/gtest.h"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;
using Error = RecordNodeRecordingStopError;
using Request = RecordNodeRecordingStopRequest;
using RequestResult = RecordNodeRecordingStopRequestResult;
using Result = RecordNodeRecordingStopResult;
using WriterOutcome = RecordThreadCleanStopOutcome;

Request request()
{
    return { 102 };
}

struct FakeOwner
{
    int ownedNodeId = 731;
    bool recordingActive = true;
    bool writerIsAvailable = true;
    WriterOutcome nextWriterOutcome =
        WriterOutcome::alreadyStoppedClean;

    int nodeIdCount = 0;
    int recordingActiveCount = 0;
    int markRecordingInactiveCount = 0;
    int markHasRecordedCount = 0;
    int advanceRecordingIndexCount = 0;
    int writerAvailableCount = 0;
    int requestWriterCleanStopCount = 0;
    int waitForWriterCleanStopUntilCount = 0;
    Clock::time_point waitedDeadline {};
    std::vector<std::string> trace;

    int nodeId()
    {
        ++nodeIdCount;
        trace.push_back ("node_id");
        return ownedNodeId;
    }

    bool isRecordingActive()
    {
        ++recordingActiveCount;
        trace.push_back ("recording_active");
        return recordingActive;
    }

    void markRecordingInactive()
    {
        ++markRecordingInactiveCount;
        trace.push_back ("mark_recording_inactive");
        recordingActive = false;
    }

    void markHasRecorded()
    {
        ++markHasRecordedCount;
        trace.push_back ("mark_has_recorded");
    }

    void advanceRecordingIndex()
    {
        ++advanceRecordingIndexCount;
        trace.push_back ("advance_recording_index");
    }

    bool writerAvailable()
    {
        ++writerAvailableCount;
        trace.push_back ("writer_available");
        return writerIsAvailable;
    }

    void requestWriterCleanStop()
    {
        ++requestWriterCleanStopCount;
        trace.push_back ("request_writer_clean_stop");
    }

    WriterOutcome waitForWriterCleanStopUntil (
        Clock::time_point deadline)
    {
        ++waitForWriterCleanStopUntilCount;
        waitedDeadline = deadline;
        trace.push_back ("wait_for_writer_clean_stop_until");
        return nextWriterOutcome;
    }
};

void expectNodeId (
    int nodeId,
    const Request& expected)
{
    EXPECT_EQ (nodeId, expected.nodeId);
}

void expectNoStateMutation (const FakeOwner& owner)
{
    EXPECT_EQ (owner.markRecordingInactiveCount, 0);
    EXPECT_EQ (owner.markHasRecordedCount, 0);
    EXPECT_EQ (owner.advanceRecordingIndexCount, 0);
}

} // namespace

TEST (RecordNodeRecordingStopTests,
      ActiveRecordingClosesIntakeThenRequestsWriterOnce)
{
    auto owner = FakeOwner {};
    const auto input = request();

    const auto result =
        requestRecordNodeRecordingStop (input, owner);

    expectNodeId (result.nodeId, input);
    EXPECT_TRUE (result.writerAvailable);
    EXPECT_FALSE (result.error.has_value());
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "recording_active",
            "mark_recording_inactive",
            "mark_has_recorded",
            "advance_recording_index",
            "writer_available",
            "request_writer_clean_stop"
        }));
    EXPECT_EQ (owner.markRecordingInactiveCount, 1);
    EXPECT_EQ (owner.markHasRecordedCount, 1);
    EXPECT_EQ (owner.advanceRecordingIndexCount, 1);
    EXPECT_EQ (owner.requestWriterCleanStopCount, 1);
    EXPECT_EQ (owner.waitForWriterCleanStopUntilCount, 0);
}

TEST (RecordNodeRecordingStopTests,
      InactiveRecordingRequestIsStateIdempotent)
{
    auto owner = FakeOwner {};
    owner.recordingActive = false;
    const auto input = request();

    const auto first =
        requestRecordNodeRecordingStop (input, owner);
    const auto second =
        requestRecordNodeRecordingStop (input, owner);

    expectNodeId (first.nodeId, input);
    expectNodeId (second.nodeId, input);
    EXPECT_TRUE (first.writerAvailable);
    EXPECT_TRUE (second.writerAvailable);
    EXPECT_FALSE (first.error.has_value());
    EXPECT_FALSE (second.error.has_value());
    expectNoStateMutation (owner);
    EXPECT_EQ (owner.requestWriterCleanStopCount, 2);
    EXPECT_EQ (owner.waitForWriterCleanStopUntilCount, 0);
}

TEST (RecordNodeRecordingStopTests,
      InactiveLingeringWriterStillReceivesCleanStopRequest)
{
    auto owner = FakeOwner {};
    owner.recordingActive = false;
    const auto input = request();

    const auto token =
        requestRecordNodeRecordingStop (input, owner);

    expectNodeId (token.nodeId, input);
    EXPECT_TRUE (token.writerAvailable);
    EXPECT_FALSE (token.error.has_value());
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "recording_active",
            "writer_available",
            "request_writer_clean_stop"
        }));
    expectNoStateMutation (owner);
    EXPECT_EQ (owner.requestWriterCleanStopCount, 1);
}

TEST (RecordNodeRecordingStopTests,
      ActiveRecordingWithoutWriterSafelyClosesIntake)
{
    auto owner = FakeOwner {};
    owner.writerIsAvailable = false;
    const auto input = request();

    const auto token =
        requestRecordNodeRecordingStop (input, owner);

    expectNodeId (token.nodeId, input);
    EXPECT_FALSE (token.writerAvailable);
    ASSERT_TRUE (token.error.has_value());
    EXPECT_EQ (*token.error, Error::writerUnavailable);
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "recording_active",
            "mark_recording_inactive",
            "mark_has_recorded",
            "advance_recording_index",
            "writer_available"
        }));
    EXPECT_EQ (owner.markRecordingInactiveCount, 1);
    EXPECT_EQ (owner.markHasRecordedCount, 1);
    EXPECT_EQ (owner.advanceRecordingIndexCount, 1);
    EXPECT_EQ (owner.requestWriterCleanStopCount, 0);
    EXPECT_EQ (owner.waitForWriterCleanStopUntilCount, 0);
}

TEST (RecordNodeRecordingStopTests,
      InactiveRecordingWithoutWriterDoesNotMutateStateOrWait)
{
    auto owner = FakeOwner {};
    owner.recordingActive = false;
    owner.writerIsAvailable = false;
    const auto input = request();

    const auto token =
        requestRecordNodeRecordingStop (input, owner);
    const auto result =
        waitForRecordNodeRecordingStopUntil (
            token,
            owner,
            Clock::time_point {} +
                std::chrono::seconds (5));

    expectNodeId (token.nodeId, input);
    EXPECT_FALSE (token.writerAvailable);
    ASSERT_TRUE (token.error.has_value());
    EXPECT_EQ (*token.error, Error::writerUnavailable);
    expectNodeId (result.nodeId, input);
    EXPECT_FALSE (result.writerThreadRunning);
    ASSERT_TRUE (result.error.has_value());
    EXPECT_EQ (*result.error, Error::writerUnavailable);
    expectNoStateMutation (owner);
    EXPECT_EQ (owner.requestWriterCleanStopCount, 0);
    EXPECT_EQ (owner.waitForWriterCleanStopUntilCount, 0);
}

TEST (RecordNodeRecordingStopTests,
      WaitMapsEveryWriterOutcomeToStableResult)
{
    struct Case
    {
        WriterOutcome writerOutcome;
        bool writerThreadRunning;
        std::optional<Error> error;
    };
    const auto cases = std::vector<Case> {
        { WriterOutcome::alreadyStoppedClean, false, std::nullopt },
        { WriterOutcome::cleanExit, false, std::nullopt },
        { WriterOutcome::alreadyStoppedUnclean,
          false,
          Error::writerExitedUncleanly },
        { WriterOutcome::uncleanExit,
          false,
          Error::writerExitedUncleanly },
        { WriterOutcome::timedOut,
          true,
          Error::writerTimedOut }
    };
    const auto input = request();
    const auto deadline = Clock::time_point {} +
        std::chrono::seconds (5);

    for (const auto& testCase : cases)
    {
        auto owner = FakeOwner {};
        owner.recordingActive = false;
        owner.nextWriterOutcome = testCase.writerOutcome;
        const auto token =
            requestRecordNodeRecordingStop (input, owner);
        const auto result =
            waitForRecordNodeRecordingStopUntil (
                token,
                owner,
                deadline);

        expectNodeId (result.nodeId, input);
        EXPECT_EQ (
            result.writerThreadRunning,
            testCase.writerThreadRunning);
        EXPECT_EQ (result.error, testCase.error);
        EXPECT_EQ (owner.waitForWriterCleanStopUntilCount, 1);
    }
}

TEST (RecordNodeRecordingStopTests,
      UnknownWriterOutcomeIsConservativelyUnclean)
{
    auto owner = FakeOwner {};
    owner.recordingActive = false;
    owner.nextWriterOutcome =
        static_cast<WriterOutcome> (999);
    const auto input = request();

    const auto token =
        requestRecordNodeRecordingStop (input, owner);
    const auto result =
        waitForRecordNodeRecordingStopUntil (
            token,
            owner,
            Clock::time_point {} +
                std::chrono::seconds (5));

    expectNodeId (result.nodeId, input);
    EXPECT_FALSE (result.writerThreadRunning);
    ASSERT_TRUE (result.error.has_value());
    EXPECT_EQ (*result.error, Error::writerExitedUncleanly);
    EXPECT_EQ (owner.waitForWriterCleanStopUntilCount, 1);
}

TEST (RecordNodeRecordingStopTests,
      WaitPassesTheExactAbsoluteDeadlineToWriter)
{
    auto owner = FakeOwner {};
    owner.recordingActive = false;
    owner.nextWriterOutcome = WriterOutcome::cleanExit;
    const auto input = request();
    const auto deadline = Clock::time_point {} +
        std::chrono::microseconds (1234567);

    const auto token =
        requestRecordNodeRecordingStop (input, owner);
    const auto result =
        waitForRecordNodeRecordingStopUntil (
            token,
            owner,
            deadline);

    EXPECT_FALSE (result.error.has_value());
    EXPECT_EQ (owner.waitForWriterCleanStopUntilCount, 1);
    EXPECT_EQ (owner.waitedDeadline, deadline);
}

TEST (RecordNodeRecordingStopTests,
      TimeoutTokenCanBeWaitedAgainWithALaterDeadline)
{
    auto owner = FakeOwner {};
    owner.recordingActive = false;
    const auto input = request();
    const auto firstDeadline = Clock::time_point {} +
        std::chrono::seconds (5);
    const auto secondDeadline = Clock::time_point {} +
        std::chrono::seconds (12);

    const auto token =
        requestRecordNodeRecordingStop (input, owner);
    owner.nextWriterOutcome = WriterOutcome::timedOut;
    const auto first =
        waitForRecordNodeRecordingStopUntil (
            token,
            owner,
            firstDeadline);
    owner.nextWriterOutcome = WriterOutcome::cleanExit;
    const auto second =
        waitForRecordNodeRecordingStopUntil (
            token,
            owner,
            secondDeadline);

    ASSERT_TRUE (first.error.has_value());
    EXPECT_EQ (*first.error, Error::writerTimedOut);
    EXPECT_TRUE (first.writerThreadRunning);
    EXPECT_FALSE (second.error.has_value());
    EXPECT_FALSE (second.writerThreadRunning);
    EXPECT_EQ (owner.requestWriterCleanStopCount, 1);
    EXPECT_EQ (owner.waitForWriterCleanStopUntilCount, 2);
    EXPECT_EQ (owner.waitedDeadline, secondDeadline);
}

TEST (RecordNodeRecordingStopTests,
      RepeatedRequestAfterTimeoutDoesNotAdvanceCountersAgain)
{
    auto owner = FakeOwner {};
    const auto input = request();
    const auto firstToken =
        requestRecordNodeRecordingStop (input, owner);
    owner.nextWriterOutcome = WriterOutcome::timedOut;
    const auto first =
        waitForRecordNodeRecordingStopUntil (
            firstToken,
            owner,
            Clock::time_point {} + std::chrono::seconds (5));
    owner.nextWriterOutcome = WriterOutcome::cleanExit;
    const auto secondToken =
        requestRecordNodeRecordingStop (input, owner);
    const auto second =
        waitForRecordNodeRecordingStopUntil (
            secondToken,
            owner,
            Clock::time_point {} + std::chrono::seconds (10));

    ASSERT_TRUE (first.error.has_value());
    EXPECT_EQ (*first.error, Error::writerTimedOut);
    EXPECT_FALSE (second.error.has_value());
    EXPECT_EQ (owner.markRecordingInactiveCount, 1);
    EXPECT_EQ (owner.markHasRecordedCount, 1);
    EXPECT_EQ (owner.advanceRecordingIndexCount, 1);
    EXPECT_EQ (owner.requestWriterCleanStopCount, 2);
    EXPECT_EQ (owner.waitForWriterCleanStopUntilCount, 2);
}

TEST (RecordNodeRecordingStopTests,
      RequestAndWaitPhasesDoNotPerformEachOthersSideEffects)
{
    auto owner = FakeOwner {};
    const auto input = request();

    const auto token =
        requestRecordNodeRecordingStop (input, owner);
    EXPECT_EQ (owner.waitForWriterCleanStopUntilCount, 0);

    const auto marksBeforeWait = std::vector<int> {
        owner.markRecordingInactiveCount,
        owner.markHasRecordedCount,
        owner.advanceRecordingIndexCount,
        owner.requestWriterCleanStopCount
    };
    owner.nextWriterOutcome = WriterOutcome::cleanExit;
    const auto result =
        waitForRecordNodeRecordingStopUntil (
            token,
            owner,
            Clock::time_point {} + std::chrono::seconds (5));

    EXPECT_FALSE (result.error.has_value());
    EXPECT_EQ (
        (std::vector<int> {
            owner.markRecordingInactiveCount,
            owner.markHasRecordedCount,
            owner.advanceRecordingIndexCount,
            owner.requestWriterCleanStopCount
        }),
        marksBeforeWait);
    EXPECT_EQ (owner.waitForWriterCleanStopUntilCount, 1);
}

TEST (RecordNodeRecordingStopTests,
      FromOwnerCopiesNodeIdentityOnceAndWaitNeverReadsItAgain)
{
    auto owner = FakeOwner {};
    owner.recordingActive = false;

    const auto token =
        requestRecordNodeRecordingStopFromOwner (owner);
    const auto result =
        waitForRecordNodeRecordingStopUntil (
            token,
            owner,
            Clock::time_point {} + std::chrono::seconds (5));

    EXPECT_EQ (token.nodeId, owner.ownedNodeId);
    EXPECT_EQ (result.nodeId, owner.ownedNodeId);
    EXPECT_EQ (owner.nodeIdCount, 1);
    EXPECT_EQ (
        owner.trace,
        (std::vector<std::string> {
            "node_id",
            "recording_active",
            "writer_available",
            "request_writer_clean_stop",
            "wait_for_writer_clean_stop_until"
        }));
}
