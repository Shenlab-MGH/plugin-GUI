#include "../../Source/Utils/StatusControl.h"

#include "gtest/gtest.h"

#include <chrono>
#include <functional>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

using namespace std::chrono_literals;

namespace
{
using Error = AcquisitionRecordingControlError;
using Mode = AcquisitionRecordingMode;
using Snapshot = AcquisitionRecordingControlSnapshot;

Snapshot snapshot (Mode mode)
{
    const auto acquiring = mode != Mode::idle;
    const auto recording = mode == Mode::record;

    Snapshot result;
    result.status = deriveAcquisitionRecordingStatus (
        acquiring,
        { { recording, recording } });
    result.recordNodes = {
        { 71, recording, recording, true, true }
    };
    result.audioDeviceAvailable = true;
    result.audioSampleRate = 44100.0;
    result.processorGraphReady = true;
    return result;
}

Snapshot inconsistentSnapshot()
{
    auto result = snapshot (Mode::idle);
    result.recordNodes[0].recordingActive = true;
    result.recordNodes[0].writerThreadRunning = true;
    result.status = deriveAcquisitionRecordingStatus (
        false,
        { { true, true } });
    return result;
}

AcquisitionRecordingControlResult success (
    Mode requested,
    Snapshot achieved,
    bool changed = true,
    bool confirmationConsumed = false)
{
    return {
        requested,
        std::move (achieved),
        changed,
        confirmationConsumed,
        std::nullopt
    };
}

AcquisitionRecordingControlResult failure (
    Mode requested,
    Error error,
    std::optional<Snapshot> achieved = std::nullopt)
{
    return {
        requested,
        std::move (achieved),
        false,
        false,
        error
    };
}

auto immediateDispatcher()
{
    return [] (std::function<void()> operation)
    {
        operation();
        return true;
    };
}

void expectTransportError (
    const StatusControlResult& result,
    int status,
    StringRef code)
{
    EXPECT_EQ (result.httpStatus, status);
    EXPECT_FALSE (result.achieved.has_value());
    EXPECT_EQ (result.errorCode, code);
    EXPECT_TRUE (result.errorMessage.isNotEmpty());
}
} // namespace

TEST (StatusControlTests,
      FreezesTheTwoSecondQueueStartDeadline)
{
    EXPECT_EQ (statusControlQueueStartTimeout, 2s);
}

TEST (StatusControlTests,
      GetReturnsOneFreshCanonicalSnapshotInsideOneDispatch)
{
    int dispatchCount = 0;
    int readCount = 0;
    bool insideDispatch = false;

    const auto result = handleStatusGet (
        [&] (std::function<void()> operation)
        {
            ++dispatchCount;
            insideDispatch = true;
            operation();
            insideDispatch = false;
            return true;
        },
        [&]
        {
            EXPECT_TRUE (insideDispatch);
            ++readCount;
            return snapshot (Mode::acquire);
        },
        50ms);

    EXPECT_EQ (result.httpStatus, 200);
    EXPECT_FALSE (result.requestedMode.has_value());
    ASSERT_TRUE (result.achieved.has_value());
    EXPECT_EQ (result.achieved->status.mode, Mode::acquire);
    EXPECT_TRUE (result.errorCode.isEmpty());
    EXPECT_EQ (dispatchCount, 1);
    EXPECT_EQ (readCount, 1);
}

TEST (StatusControlTests,
      GetReturnsRawInconsistentStateAsHttp500)
{
    const auto result = handleStatusGet (
        immediateDispatcher(),
        [] { return inconsistentSnapshot(); },
        50ms);

    EXPECT_EQ (result.httpStatus, 500);
    EXPECT_EQ (result.errorCode, "inconsistent_state");
    EXPECT_TRUE (result.errorMessage.isNotEmpty());
    ASSERT_TRUE (result.achieved.has_value());
    EXPECT_FALSE (result.achieved->status.mode.has_value());
    EXPECT_FALSE (result.achieved->status.recordingConsistent);
}

TEST (StatusControlTests,
      GetRejectsContradictoryCopiedNodeFacts)
{
    auto contradictory = snapshot (Mode::idle);
    contradictory.recordNodes[0].generation = 0;

    const auto result = handleStatusGet (
        immediateDispatcher(),
        [contradictory] { return contradictory; },
        50ms);

    EXPECT_EQ (result.httpStatus, 500);
    EXPECT_EQ (result.errorCode, "inconsistent_state");
    ASSERT_TRUE (result.achieved.has_value());
    EXPECT_EQ (result.achieved->recordNodes[0].generation, 0u);
}

TEST (StatusControlTests,
      GetMapsUnavailableTimeoutAndExceptionWithoutFabricatingState)
{
    int readCount = 0;
    auto read = [&]
    {
        ++readCount;
        return snapshot (Mode::idle);
    };

    auto result = handleStatusGet (
        [] (std::function<void()>) { return false; },
        read,
        50ms);
    expectTransportError (
        result,
        503,
        "operation_unavailable");
    EXPECT_EQ (readCount, 0);

    std::function<void()> queued;
    result = handleStatusGet (
        [&] (std::function<void()> operation)
        {
            queued = std::move (operation);
            return true;
        },
        read,
        1ms);
    expectTransportError (
        result,
        504,
        "operation_timeout");
    EXPECT_EQ (readCount, 0);
    ASSERT_TRUE (queued);
    queued();
    EXPECT_EQ (readCount, 0);

    result = handleStatusGet (
        immediateDispatcher(),
        []() -> Snapshot
        {
            throw std::runtime_error ("read failed");
        },
        50ms);
    expectTransportError (
        result,
        500,
        "operation_failed");
    EXPECT_TRUE (result.errorMessage.contains ("read failed"));
}

TEST (StatusControlTests,
      PutStrictlyParsesBeforeDispatch)
{
    int dispatchCount = 0;
    int applyCount = 0;

    const auto result = handleStatusPut (
        R"({"mode":"idle"})",
        [&] (std::function<void()>)
        {
            ++dispatchCount;
            return true;
        },
        [&] (const StatusRequest& request)
        {
            ++applyCount;
            return success (
                request.mode,
                snapshot (request.mode));
        },
        50ms);

    EXPECT_EQ (result.httpStatus, 400);
    EXPECT_EQ (result.errorCode, "invalid_request");
    EXPECT_TRUE (result.errorMessage.isNotEmpty());
    EXPECT_FALSE (result.requestedMode.has_value());
    EXPECT_FALSE (result.achieved.has_value());
    EXPECT_EQ (dispatchCount, 0);
    EXPECT_EQ (applyCount, 0);
}

TEST (StatusControlTests,
      PutReturnsExactRequestedAndAchievedStateFromOneDispatch)
{
    std::optional<StatusRequest> applied;
    int dispatchCount = 0;
    int applyCount = 0;
    bool insideDispatch = false;

    const auto result = handleStatusPut (
        R"({"mode":"RECORD","confirm_unsynchronized":true})",
        [&] (std::function<void()> operation)
        {
            ++dispatchCount;
            insideDispatch = true;
            operation();
            insideDispatch = false;
            return true;
        },
        [&] (const StatusRequest& request)
        {
            EXPECT_TRUE (insideDispatch);
            ++applyCount;
            applied = request;
            return success (
                request.mode,
                snapshot (Mode::record),
                true,
                true);
        },
        50ms);

    EXPECT_EQ (result.httpStatus, 200);
    ASSERT_TRUE (result.requestedMode.has_value());
    EXPECT_EQ (*result.requestedMode, Mode::record);
    EXPECT_TRUE (result.changed);
    EXPECT_TRUE (result.unsynchronizedConfirmed);
    EXPECT_TRUE (result.errorCode.isEmpty());
    ASSERT_TRUE (result.achieved.has_value());
    EXPECT_EQ (result.achieved->status.mode, Mode::record);
    ASSERT_TRUE (applied.has_value());
    EXPECT_TRUE (applied->confirmUnsynchronized);
    EXPECT_EQ (dispatchCount, 1);
    EXPECT_EQ (applyCount, 1);
}

struct ControllerErrorMappingCase
{
    const char* name;
    Error error;
    int httpStatus;
    const char* code;
};

class StatusControlErrorMappingTests
    : public testing::TestWithParam<
          ControllerErrorMappingCase>
{
};

TEST_P (StatusControlErrorMappingTests,
        PutUsesTheFrozenControllerErrorMapping)
{
    const auto testCase = GetParam();
    const auto result = handleStatusPut (
        R"({"mode":"RECORD"})",
        immediateDispatcher(),
        [&] (const StatusRequest& request)
        {
            return failure (
                request.mode,
                testCase.error,
                snapshot (Mode::acquire));
        },
        50ms);

    EXPECT_EQ (result.httpStatus, testCase.httpStatus);
    EXPECT_EQ (result.errorCode, testCase.code);
    EXPECT_TRUE (result.errorMessage.isNotEmpty());
    EXPECT_FALSE (result.changed);
    ASSERT_TRUE (result.achieved.has_value());
    EXPECT_EQ (result.achieved->status.mode, Mode::acquire);
}

INSTANTIATE_TEST_SUITE_P (
    FrozenHttpStatus,
    StatusControlErrorMappingTests,
    testing::Values (
        ControllerErrorMappingCase {
            "RecordNodeRequired", Error::recordNodeRequired,
            409, "record_node_required" },
        ControllerErrorMappingCase {
            "InvalidRecordingPath", Error::invalidRecordingPath,
            409, "invalid_recording_path" },
        ControllerErrorMappingCase {
            "UnsynchronizedConfirmationRequired",
            Error::unsynchronizedConfirmationRequired,
            409, "unsynchronized_confirmation_required" },
        ControllerErrorMappingCase {
            "AudioDeviceUnavailable", Error::audioDeviceUnavailable,
            409, "audio_device_unavailable" },
        ControllerErrorMappingCase {
            "AudioSampleRateTooLow", Error::audioSampleRateTooLow,
            409, "audio_sample_rate_too_low" },
        ControllerErrorMappingCase {
            "ProcessorGraphNotReady", Error::processorGraphNotReady,
            409, "processor_graph_not_ready" },
        ControllerErrorMappingCase {
            "StateTransitionRejected", Error::stateTransitionRejected,
            409, "state_transition_rejected" },
        ControllerErrorMappingCase {
            "RecordingDirectoryCreateFailed",
            Error::recordingDirectoryCreateFailed,
            500, "recording_directory_create_failed" },
        ControllerErrorMappingCase {
            "RecordingStartFailed", Error::recordingStartFailed,
            500, "recording_start_failed" },
        ControllerErrorMappingCase {
            "RollbackFailed", Error::rollbackFailed,
            500, "rollback_failed" },
        ControllerErrorMappingCase {
            "InconsistentState", Error::inconsistentState,
            500, "inconsistent_state" },
        ControllerErrorMappingCase {
            "OperationFailed", Error::operationFailed,
            500, "operation_failed" }),
    [] (const testing::TestParamInfo<
        ControllerErrorMappingCase>& info)
    {
        return std::string (info.param.name);
    });

TEST (StatusControlTests,
      PutCancelsOnlyAnOperationThatHasNotStarted)
{
    int applyCount = 0;
    std::function<void()> queued;
    const auto result = handleStatusPut (
        R"({"mode":"ACQUIRE"})",
        [&] (std::function<void()> operation)
        {
            queued = std::move (operation);
            return true;
        },
        [&] (const StatusRequest& request)
        {
            ++applyCount;
            return success (
                request.mode,
                snapshot (request.mode));
        },
        1ms);

    expectTransportError (
        result,
        504,
        "operation_timeout");
    ASSERT_TRUE (result.requestedMode.has_value());
    EXPECT_EQ (*result.requestedMode, Mode::acquire);
    EXPECT_EQ (applyCount, 0);
    ASSERT_TRUE (queued);
    queued();
    EXPECT_EQ (applyCount, 0);
}

TEST (StatusControlTests,
      PutWaitsForADeterminateResultOnceStarted)
{
    std::promise<void> applyStarted;
    auto started = applyStarted.get_future().share();
    std::promise<void> releaseApply;
    auto release = releaseApply.get_future().share();
    std::thread messageThread;
    std::thread releaser (
        [&]
        {
            started.wait();
            releaseApply.set_value();
        });

    const auto result = handleStatusPut (
        R"({"mode":"ACQUIRE"})",
        [&] (std::function<void()> operation)
        {
            messageThread = std::thread (
                [operation = std::move (operation)]() mutable
                {
                    operation();
                });
            started.wait();
            return true;
        },
        [&] (const StatusRequest& request)
        {
            applyStarted.set_value();
            release.wait();
            return success (
                request.mode,
                snapshot (request.mode));
        },
        0ms);

    messageThread.join();
    releaser.join();
    EXPECT_EQ (result.httpStatus, 200);
    ASSERT_TRUE (result.achieved.has_value());
    EXPECT_EQ (result.achieved->status.mode, Mode::acquire);
}

TEST (StatusControlTests,
      PutRejectsMalformedCompletedControllerResults)
{
    struct Case
    {
        AcquisitionRecordingControlResult result;
        const char* code;
    };

    for (const auto& testCase : {
             Case { { Mode::record, std::nullopt, true,
                      false, std::nullopt },
                    "operation_failed" },
             Case { { Mode::record, inconsistentSnapshot(), true,
                      false, std::nullopt },
                    "inconsistent_state" },
             Case { { Mode::record, snapshot (Mode::acquire), true,
                      false, std::nullopt },
                    "operation_failed" },
             Case { { Mode::idle, snapshot (Mode::record), true,
                      false, std::nullopt },
                    "operation_failed" } })
    {
        const auto result = handleStatusPut (
            R"({"mode":"RECORD"})",
            immediateDispatcher(),
            [&] (const StatusRequest&)
            {
                return testCase.result;
            },
            50ms);

        EXPECT_EQ (result.httpStatus, 500);
        EXPECT_EQ (result.errorCode, testCase.code);
        EXPECT_FALSE (result.changed);
    }
}

TEST (StatusControlTests,
      PutRejectsAConfirmationTheRequestDidNotGrant)
{
    const auto result = handleStatusPut (
        R"({"mode":"RECORD"})",
        immediateDispatcher(),
        [] (const StatusRequest& request)
        {
            return success (
                request.mode,
                snapshot (Mode::record),
                true,
                true);
        },
        50ms);

    EXPECT_EQ (result.httpStatus, 500);
    EXPECT_EQ (result.errorCode, "operation_failed");
    EXPECT_FALSE (result.changed);
    EXPECT_FALSE (result.unsynchronizedConfirmed);
    ASSERT_TRUE (result.achieved.has_value());
    EXPECT_EQ (result.achieved->status.mode, Mode::record);
}

TEST (StatusControlTests,
      PutMapsStartedExceptionsWithoutFabricatingState)
{
    const auto result = handleStatusPut (
        R"({"mode":"IDLE"})",
        immediateDispatcher(),
        [] (const StatusRequest&)
            -> AcquisitionRecordingControlResult
        {
            throw std::runtime_error ("controller failed");
        },
        50ms);

    expectTransportError (
        result,
        500,
        "operation_failed");
    EXPECT_TRUE (
        result.errorMessage.contains ("controller failed"));
}
