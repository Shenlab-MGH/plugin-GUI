#include "../../Source/Utils/StatusControl.h"

#include "gtest/gtest.h"

#include <chrono>
#include <functional>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace
{
using Error = AcquisitionRecordingControlError;
using Mode = AcquisitionRecordingMode;
using Snapshot = AcquisitionRecordingControlSnapshot;

Snapshot snapshot (Mode mode)
{
    const auto callbacksActive =
        mode != Mode::idle;
    const auto recordingActive =
        mode == Mode::record;

    Snapshot result;
    result.status = deriveAcquisitionRecordingStatus (
        callbacksActive,
        { { recordingActive, recordingActive } });
    result.recordNodes = {
        { 71,
          recordingActive,
          recordingActive,
          true,
          true }
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
    Mode requestedMode,
    Snapshot achieved,
    bool changed = true,
    bool confirmationConsumed = false)
{
    return {
        requestedMode,
        std::move (achieved),
        changed,
        confirmationConsumed,
        std::nullopt
    };
}

AcquisitionRecordingControlResult failure (
    Mode requestedMode,
    Error error,
    Snapshot achieved)
{
    return {
        requestedMode,
        std::move (achieved),
        false,
        false,
        error
    };
}

void expectTransportError (
    const StatusControlResult& result,
    int httpStatus,
    StringRef errorCode)
{
    EXPECT_EQ (result.httpStatus, httpStatus);
    EXPECT_FALSE (result.achieved.has_value());
    EXPECT_EQ (result.errorCode, errorCode);
    EXPECT_TRUE (result.errorMessage.isNotEmpty());
}
} // namespace

TEST (StatusControlTests,
      FreezesTheTwoSecondTransportQueueStartDeadline)
{
    EXPECT_EQ (statusControlTransportTimeout, 2s);
}

TEST (StatusControlTests,
      GetReturnsEveryCanonicalModeFromFreshReadback)
{
    for (const auto mode :
         { Mode::idle, Mode::acquire, Mode::record })
    {
        int readCount = 0;
        const auto result = handleStatusGet (
            [] (std::function<void()> operation)
            {
                operation();
                return true;
            },
            [&]
            {
                ++readCount;
                return snapshot (mode);
            },
            50ms);

        EXPECT_EQ (result.httpStatus, 200);
        ASSERT_TRUE (result.achieved.has_value());
        EXPECT_EQ (result.achieved->status.mode, mode);
        EXPECT_TRUE (
            result.achieved->status.recordingConsistent);
        EXPECT_EQ (readCount, 1);
    }
}

TEST (StatusControlTests,
      GetReturnsOneFreshConsistentAchievedSnapshot)
{
    auto current = snapshot (Mode::idle);
    int dispatchCount = 0;
    int readCount = 0;
    bool insideDispatch = false;

    current = snapshot (Mode::acquire);
    const auto result = handleStatusGet (
        [&] (std::function<void()> operation)
        {
            ++dispatchCount;
            EXPECT_FALSE (insideDispatch);
            insideDispatch = true;
            operation();
            insideDispatch = false;
            return true;
        },
        [&]
        {
            EXPECT_TRUE (insideDispatch);
            ++readCount;
            return current;
        },
        50ms);

    EXPECT_EQ (result.httpStatus, 200);
    EXPECT_TRUE (result.errorCode.isEmpty());
    EXPECT_EQ (dispatchCount, 1);
    EXPECT_EQ (readCount, 1);
    EXPECT_FALSE (result.requestedMode.has_value());
    ASSERT_TRUE (result.achieved.has_value());
    EXPECT_EQ (result.achieved->status.mode, Mode::acquire);
    EXPECT_TRUE (
        result.achieved->status.acquisitionActive);
    EXPECT_FALSE (
        result.achieved->status.recordingActive);
    EXPECT_TRUE (
        result.achieved->status.recordingConsistent);
}

TEST (StatusControlTests,
      GetReturnsInconsistentStateWithTheRawCopiedSnapshot)
{
    const auto raw = inconsistentSnapshot();
    const auto result = handleStatusGet (
        [] (std::function<void()> operation)
        {
            operation();
            return true;
        },
        [raw] { return raw; },
        50ms);

    EXPECT_EQ (result.httpStatus, 500);
    EXPECT_EQ (result.errorCode, "inconsistent_state");
    EXPECT_TRUE (result.errorMessage.isNotEmpty());
    ASSERT_TRUE (result.achieved.has_value());
    EXPECT_FALSE (
        result.achieved->status.mode.has_value());
    EXPECT_FALSE (
        result.achieved->status.acquisitionActive);
    EXPECT_FALSE (
        result.achieved->status.recordingActive);
    EXPECT_EQ (
        result.achieved->status.recordNodeCount,
        1u);
    EXPECT_EQ (
        result.achieved->status.activeRecordNodeCount,
        1u);
    EXPECT_EQ (
        result.achieved->status.writerThreadRunningCount,
        1u);
    EXPECT_FALSE (
        result.achieved->status.recordingConsistent);
}

TEST (StatusControlTests,
      GetMapsUnavailableAndCancelsAQueueStartTimeout)
{
    int readCount = 0;
    auto result = handleStatusGet (
        [] (std::function<void()>) { return false; },
        [&]
        {
            ++readCount;
            return snapshot (Mode::idle);
        },
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
        [&]
        {
            ++readCount;
            return snapshot (Mode::idle);
        },
        1ms);

    expectTransportError (
        result,
        504,
        "operation_timeout");
    EXPECT_EQ (readCount, 0);
    ASSERT_TRUE (queued);
    queued();
    EXPECT_EQ (readCount, 0);
}

TEST (StatusControlTests,
      GetMapsDispatchAndReadExceptionsWithoutFabricatingState)
{
    int readCount = 0;
    auto result = handleStatusGet (
        [] (std::function<void()>) -> bool
        {
            throw std::runtime_error (
                "dispatch failed");
        },
        [&]
        {
            ++readCount;
            return snapshot (Mode::idle);
        },
        50ms);

    expectTransportError (
        result,
        503,
        "operation_unavailable");
    EXPECT_TRUE (
        result.errorMessage.contains (
            "dispatch failed"));
    EXPECT_EQ (readCount, 0);

    result = handleStatusGet (
        [] (std::function<void()> operation)
        {
            operation();
            return true;
        },
        [&]() -> Snapshot
        {
            ++readCount;
            throw std::runtime_error ("read failed");
        },
        50ms);

    expectTransportError (
        result,
        500,
        "operation_failed");
    EXPECT_TRUE (
        result.errorMessage.contains ("read failed"));
    EXPECT_EQ (readCount, 1);
}

TEST (StatusControlTests,
      GetWaitsForADeterminateReadOnceItStarts)
{
    std::promise<void> readStarted;
    auto started =
        readStarted.get_future().share();
    std::promise<void> releaseRead;
    auto release =
        releaseRead.get_future().share();
    std::thread messageThread;
    std::thread releaser (
        [&]
        {
            started.wait();
            releaseRead.set_value();
        });

    const auto result = handleStatusGet (
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
        [&]
        {
            readStarted.set_value();
            release.wait();
            return snapshot (Mode::record);
        },
        0ms);

    messageThread.join();
    releaser.join();
    EXPECT_EQ (result.httpStatus, 200);
    ASSERT_TRUE (result.achieved.has_value());
    EXPECT_EQ (
        result.achieved->status.mode,
        Mode::record);
}

TEST (StatusControlTests,
      PutStrictlyParsesBeforeDispatch)
{
    int dispatchCount = 0;
    int controllerCount = 0;

    const auto result = handleStatusPut (
        R"({"mode":"idle"})",
        [&] (std::function<void()>)
        {
            ++dispatchCount;
            return true;
        },
        [&] (const StatusRequest&)
        {
            ++controllerCount;
            return success (
                Mode::idle,
                snapshot (Mode::idle));
        },
        50ms);

    EXPECT_EQ (result.httpStatus, 400);
    EXPECT_EQ (result.errorCode, "invalid_request");
    EXPECT_TRUE (result.errorMessage.isNotEmpty());
    EXPECT_FALSE (result.requestedMode.has_value());
    EXPECT_FALSE (result.achieved.has_value());
    EXPECT_EQ (dispatchCount, 0);
    EXPECT_EQ (controllerCount, 0);
}

TEST (StatusControlTests,
      PutReturnsTheExactRequestedAndAchievedSuccess)
{
    std::optional<StatusRequest> applied;
    int dispatchCount = 0;
    int controllerCount = 0;
    bool insideDispatch = false;
    const auto result = handleStatusPut (
        R"({"mode":"RECORD","confirm_unsynchronized":true})",
        [&] (std::function<void()> operation)
        {
            ++dispatchCount;
            EXPECT_FALSE (insideDispatch);
            insideDispatch = true;
            operation();
            insideDispatch = false;
            return true;
        },
        [&] (const StatusRequest& request)
        {
            EXPECT_TRUE (insideDispatch);
            ++controllerCount;
            applied = request;
            return success (
                Mode::record,
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
    EXPECT_TRUE (
        result.achieved->status.recordingConsistent);
    ASSERT_TRUE (applied.has_value());
    EXPECT_EQ (applied->mode, Mode::record);
    EXPECT_TRUE (applied->confirmUnsynchronized);
    EXPECT_EQ (dispatchCount, 1);
    EXPECT_EQ (controllerCount, 1);
}

TEST (StatusControlTests,
      PutReturnsEveryRequestedCanonicalMode)
{
    struct Case
    {
        const char* body;
        Mode mode;
    };

    for (const auto& testCase : {
             Case { R"({"mode":"IDLE"})",
                    Mode::idle },
             Case { R"({"mode":"ACQUIRE"})",
                    Mode::acquire },
             Case { R"({"mode":"RECORD"})",
                    Mode::record },
             Case {
                    R"({"mode":"RECORD","confirm_unsynchronized":false})",
                    Mode::record } })
    {
        int controllerCount = 0;
        const auto result = handleStatusPut (
            testCase.body,
            [] (std::function<void()> operation)
            {
                operation();
                return true;
            },
            [&] (const StatusRequest& request)
            {
                ++controllerCount;
                EXPECT_EQ (request.mode, testCase.mode);
                EXPECT_FALSE (
                    request.confirmUnsynchronized);
                return success (
                    request.mode,
                    snapshot (request.mode));
            },
            50ms);

        EXPECT_EQ (result.httpStatus, 200);
        ASSERT_TRUE (result.requestedMode.has_value());
        EXPECT_EQ (*result.requestedMode, testCase.mode);
        ASSERT_TRUE (result.achieved.has_value());
        EXPECT_EQ (
            result.achieved->status.mode,
            testCase.mode);
        EXPECT_EQ (controllerCount, 1);
    }
}

struct ControllerErrorMappingCase
{
    const char* name;
    Error error;
    int httpStatus;
    const char* expectedCode;
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
    const auto achieved = snapshot (Mode::acquire);
    const auto result = handleStatusPut (
        R"({"mode":"RECORD"})",
        [] (std::function<void()> operation)
        {
            operation();
            return true;
        },
        [&] (const StatusRequest& request)
        {
            return failure (
                request.mode,
                testCase.error,
                achieved);
        },
        50ms);

    EXPECT_EQ (result.httpStatus,
               testCase.httpStatus);
    ASSERT_TRUE (result.requestedMode.has_value());
    EXPECT_EQ (*result.requestedMode, Mode::record);
    EXPECT_FALSE (result.changed);
    EXPECT_FALSE (result.unsynchronizedConfirmed);
    EXPECT_EQ (
        result.errorCode,
        testCase.expectedCode);
    EXPECT_TRUE (result.errorMessage.isNotEmpty());
    ASSERT_TRUE (result.achieved.has_value());
    EXPECT_EQ (
        result.achieved->status.mode,
        Mode::acquire);
}

INSTANTIATE_TEST_SUITE_P (
    FrozenHttpStatus,
    StatusControlErrorMappingTests,
    testing::Values (
        ControllerErrorMappingCase {
            "RecordNodeRequired",
            Error::recordNodeRequired,
            409,
            "record_node_required"
        },
        ControllerErrorMappingCase {
            "InvalidRecordingPath",
            Error::invalidRecordingPath,
            409,
            "invalid_recording_path"
        },
        ControllerErrorMappingCase {
            "UnsynchronizedConfirmationRequired",
            Error::unsynchronizedConfirmationRequired,
            409,
            "unsynchronized_confirmation_required"
        },
        ControllerErrorMappingCase {
            "AudioDeviceUnavailable",
            Error::audioDeviceUnavailable,
            409,
            "audio_device_unavailable"
        },
        ControllerErrorMappingCase {
            "AudioSampleRateTooLow",
            Error::audioSampleRateTooLow,
            409,
            "audio_sample_rate_too_low"
        },
        ControllerErrorMappingCase {
            "ProcessorGraphNotReady",
            Error::processorGraphNotReady,
            409,
            "processor_graph_not_ready"
        },
        ControllerErrorMappingCase {
            "StateTransitionRejected",
            Error::stateTransitionRejected,
            409,
            "state_transition_rejected"
        },
        ControllerErrorMappingCase {
            "RecordingDirectoryCreateFailed",
            Error::recordingDirectoryCreateFailed,
            500,
            "recording_directory_create_failed"
        },
        ControllerErrorMappingCase {
            "RecordingStartFailed",
            Error::recordingStartFailed,
            500,
            "recording_start_failed"
        },
        ControllerErrorMappingCase {
            "RollbackFailed",
            Error::rollbackFailed,
            500,
            "rollback_failed"
        },
        ControllerErrorMappingCase {
            "InconsistentState",
            Error::inconsistentState,
            500,
            "inconsistent_state"
        },
        ControllerErrorMappingCase {
            "OperationFailed",
            Error::operationFailed,
            500,
            "operation_failed"
        }),
    [] (const testing::TestParamInfo<
        ControllerErrorMappingCase>& info)
    {
        return std::string (info.param.name);
    });

TEST (StatusControlTests,
      PutMapsUnavailableAndCancelsAQueueStartTimeout)
{
    int controllerCount = 0;
    auto apply = [&] (const StatusRequest& request)
    {
        ++controllerCount;
        return success (
            request.mode,
            snapshot (request.mode));
    };

    auto result = handleStatusPut (
        R"({"mode":"ACQUIRE"})",
        [] (std::function<void()>) { return false; },
        apply,
        50ms);

    expectTransportError (
        result,
        503,
        "operation_unavailable");
    EXPECT_EQ (controllerCount, 0);

    std::function<void()> queued;
    result = handleStatusPut (
        R"({"mode":"ACQUIRE"})",
        [&] (std::function<void()> operation)
        {
            queued = std::move (operation);
            return true;
        },
        apply,
        1ms);

    expectTransportError (
        result,
        504,
        "operation_timeout");
    EXPECT_EQ (controllerCount, 0);
    ASSERT_TRUE (queued);
    queued();
    EXPECT_EQ (controllerCount, 0);
}

TEST (StatusControlTests,
      PutMapsDispatchExceptionToUnavailableWithoutControllerCall)
{
    int controllerCount = 0;
    const auto result = handleStatusPut (
        R"({"mode":"ACQUIRE"})",
        [] (std::function<void()>) -> bool
        {
            throw std::runtime_error (
                "dispatch failed");
        },
        [&] (const StatusRequest& request)
        {
            ++controllerCount;
            return success (
                request.mode,
                snapshot (request.mode));
        },
        50ms);

    expectTransportError (
        result,
        503,
        "operation_unavailable");
    EXPECT_TRUE (
        result.errorMessage.contains (
            "dispatch failed"));
    EXPECT_EQ (controllerCount, 0);
}

TEST (StatusControlTests,
      PutControllerErrorWithoutAchievedStateDoesNotFabricateState)
{
    const auto result = handleStatusPut (
        R"({"mode":"RECORD","confirm_unsynchronized":false})",
        [] (std::function<void()> operation)
        {
            operation();
            return true;
        },
        [] (const StatusRequest& request)
        {
            EXPECT_FALSE (
                request.confirmUnsynchronized);
            return AcquisitionRecordingControlResult {
                request.mode,
                std::nullopt,
                false,
                false,
                Error::recordingStartFailed
            };
        },
        50ms);

    EXPECT_EQ (result.httpStatus, 500);
    EXPECT_EQ (
        result.errorCode,
        "recording_start_failed");
    EXPECT_FALSE (result.changed);
    EXPECT_FALSE (result.achieved.has_value());
}

TEST (StatusControlTests,
      PutDefensivelyRejectsMalformedCompletedControllerResults)
{
    struct Case
    {
        AcquisitionRecordingControlResult result;
        const char* expectedCode;
    };

    for (const auto& testCase : {
             Case {
                 {
                     Mode::record,
                     std::nullopt,
                     true,
                     false,
                     std::nullopt
                 },
                 "operation_failed"
             },
             Case {
                 {
                     Mode::record,
                     inconsistentSnapshot(),
                     true,
                     false,
                     std::nullopt
                 },
                 "inconsistent_state"
             },
             Case {
                 {
                     Mode::record,
                     snapshot (Mode::acquire),
                     true,
                     false,
                     std::nullopt
                 },
                 "operation_failed"
             } })
    {
        const auto result = handleStatusPut (
            R"({"mode":"RECORD"})",
            [] (std::function<void()> operation)
            {
                operation();
                return true;
            },
            [&] (const StatusRequest&)
            {
                return testCase.result;
            },
            50ms);

        EXPECT_EQ (result.httpStatus, 500);
        EXPECT_NE (result.httpStatus, 200);
        EXPECT_EQ (
            result.errorCode,
            testCase.expectedCode);
        EXPECT_FALSE (result.changed);
    }
}

TEST (StatusControlTests,
      PutWaitsForADeterminateResultOnceTheOperationStarts)
{
    std::promise<void> controllerStarted;
    auto started =
        controllerStarted.get_future().share();
    std::promise<void> releaseController;
    auto release =
        releaseController.get_future().share();
    std::thread messageThread;
    std::thread releaser (
        [&]
        {
            started.wait();
            releaseController.set_value();
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
            controllerStarted.set_value();
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
    EXPECT_EQ (
        result.achieved->status.mode,
        Mode::acquire);
}

TEST (StatusControlTests,
      PutMapsAStartedUnexpectedFailureToHttp500)
{
    const auto result = handleStatusPut (
        R"({"mode":"IDLE"})",
        [] (std::function<void()> operation)
        {
            operation();
            return true;
        },
        [] (const StatusRequest&)
            -> AcquisitionRecordingControlResult
        {
            throw std::runtime_error (
                "controller failed");
        },
        50ms);

    EXPECT_EQ (result.httpStatus, 500);
    EXPECT_EQ (result.errorCode, "operation_failed");
    EXPECT_TRUE (
        result.errorMessage.contains (
            "controller failed"));
    EXPECT_FALSE (result.achieved.has_value());
}
