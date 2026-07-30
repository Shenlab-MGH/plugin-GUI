#include "../../Source/Utils/AcquisitionRecordingControl.h"

#include "gtest/gtest.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace
{
using Error = AcquisitionRecordingControlError;
using Mode = AcquisitionRecordingMode;
using Node = AcquisitionRecordingControlNodeState;
using Result = AcquisitionRecordingControlResult;

struct FakeOwner
{
    bool callbacksActive = false;
    bool audioDeviceAvailable = true;
    double audioSampleRate = 44100.0;
    bool processorGraphReady = true;
    std::vector<Node> nodes;

    bool startAcquisitionSucceeds = true;
    bool stopAcquisitionSucceeds = true;
    bool stopRecordingSucceeds = true;
    bool rollbackSucceeds = true;
    std::optional<Error> recordingStartError;
    std::vector<std::uint64_t> generationsStartedByRecording;
    bool throwWhileStartingAcquisition = false;
    bool throwWhileStartingRecording = false;
    int readNumberToThrow = 0;

    int readCount = 0;
    int startAcquisitionCount = 0;
    int stopAcquisitionCount = 0;
    int startRecordingCount = 0;
    int stopRecordingCount = 0;
    int rollbackCount = 0;
    bool lastConfirmation = false;
    std::vector<std::uint64_t> rolledBackGenerations;
    std::vector<std::string> actionOrder;
    std::function<void()> beforeStartAcquisition;

    AcquisitionRecordingControlSnapshot read()
    {
        ++readCount;
        if (readCount == readNumberToThrow)
            throw std::runtime_error ("readback failed");

        std::vector<RecordNodeRuntimeState> runtimeNodes;
        runtimeNodes.reserve (nodes.size());
        for (const auto& node : nodes)
        {
            runtimeNodes.push_back ({
                node.recordingActive,
                node.writerThreadRunning
            });
        }

        return {
            deriveAcquisitionRecordingStatus (
                callbacksActive,
                runtimeNodes),
            nodes,
            audioDeviceAvailable,
            audioSampleRate,
            processorGraphReady
        };
    }

    AcquisitionRecordingOwnerActions actions()
    {
        return {
            [this] { return read(); },
            [this] (
                const AcquisitionRecordingRequestOwnershipReporter&
                    ownership)
            {
                ++startAcquisitionCount;
                actionOrder.push_back ("start_acquisition");
                if (beforeStartAcquisition)
                    beforeStartAcquisition();
                if (startAcquisitionSucceeds)
                {
                    callbacksActive = true;
                    if (ownership.acquisitionStarted)
                        ownership.acquisitionStarted();
                }
                if (throwWhileStartingAcquisition)
                    throw std::runtime_error (
                        "start acquisition failed");
                return startAcquisitionSucceeds;
            },
            [this]
            {
                ++stopAcquisitionCount;
                actionOrder.push_back ("stop_acquisition");
                if (stopAcquisitionSucceeds)
                    callbacksActive = false;
                return stopAcquisitionSucceeds;
            },
            [this] (
                bool confirmation,
                const AcquisitionRecordingRequestOwnershipReporter&
                    ownership)
            {
                ++startRecordingCount;
                lastConfirmation = confirmation;
                actionOrder.push_back ("start_recording");

                auto generations =
                    generationsStartedByRecording;
                if (generations.empty()
                    && ! recordingStartError.has_value())
                {
                    for (const auto& node : nodes)
                        generations.push_back (
                            node.generation);
                }

                for (auto generation : generations)
                {
                    for (auto& node : nodes)
                    {
                        if (node.generation == generation)
                        {
                            node.recordingActive = true;
                            node.writerThreadRunning = true;
                            if (ownership.recordNodeStarted)
                            {
                                ownership.recordNodeStarted (
                                    generation);
                            }
                        }
                    }
                }

                if (throwWhileStartingRecording)
                    throw std::runtime_error (
                        "start recording failed");

                return AcquisitionRecordingStartResult {
                    generations,
                    recordingStartError
                };
            },
            [this]
            {
                ++stopRecordingCount;
                actionOrder.push_back ("stop_recording");
                if (stopRecordingSucceeds)
                {
                    for (auto& node : nodes)
                    {
                        node.recordingActive = false;
                        node.writerThreadRunning = false;
                    }
                }
                return stopRecordingSucceeds;
            },
            [this] (
                const std::vector<std::uint64_t>& generations)
            {
                ++rollbackCount;
                actionOrder.push_back ("rollback_recording");
                rolledBackGenerations = generations;
                if (rollbackSucceeds)
                {
                    for (auto generation : generations)
                    {
                        for (auto& node : nodes)
                        {
                            if (node.generation == generation)
                            {
                                node.recordingActive = false;
                                node.writerThreadRunning = false;
                            }
                        }
                    }
                }
                return rollbackSucceeds;
            }
        };
    }
};

Node node (std::uint64_t generation,
           bool active = false,
           bool writerRunning = false,
           bool pathValid = true,
           bool synchronized = true)
{
    return {
        generation,
        active,
        writerRunning,
        pathValid,
        synchronized
    };
}

StatusRequest request (Mode mode,
                       bool confirmation = false)
{
    return { mode, confirmation };
}

void expectSuccess (const Result& result,
                    Mode requested,
                    bool changed,
                    bool confirmationConsumed = false)
{
    EXPECT_FALSE (result.error.has_value());
    EXPECT_EQ (result.requestedMode, requested);
    ASSERT_TRUE (result.achieved.has_value());
    ASSERT_TRUE (result.achieved->status.mode.has_value());
    EXPECT_EQ (*result.achieved->status.mode, requested);
    EXPECT_TRUE (
        result.achieved->status.recordingConsistent);
    EXPECT_EQ (result.changed, changed);
    EXPECT_EQ (result.unsynchronizedConfirmed,
               confirmationConsumed);
}

void expectError (const Result& result,
                  Mode requested,
                  Error error)
{
    ASSERT_TRUE (result.error.has_value());
    EXPECT_EQ (*result.error, error);
    EXPECT_EQ (result.requestedMode, requested);
    EXPECT_FALSE (result.changed);
    EXPECT_STREQ (
        acquisitionRecordingControlErrorCode (error),
        acquisitionRecordingControlErrorCode (
            *result.error));
}
} // namespace

TEST (AcquisitionRecordingReadinessTests,
      IsPureAndReturnsEveryFrozenPrecondition)
{
    FakeOwner owner;
    owner.nodes = { node (1) };
    const auto baseline = owner.read();

    auto readiness = evaluateAcquisitionRecordingReadiness (
        baseline,
        request (Mode::record),
        false);
    EXPECT_FALSE (readiness.error.has_value());
    EXPECT_FALSE (readiness.unsynchronizedConfirmationConsumed);

    auto facts = baseline;
    facts.status = deriveAcquisitionRecordingStatus (
        false,
        { { true, true } });
    readiness = evaluateAcquisitionRecordingReadiness (
        facts,
        request (Mode::idle),
        false);
    EXPECT_EQ (readiness.error, Error::inconsistentState);

    readiness = evaluateAcquisitionRecordingReadiness (
        baseline,
        request (Mode::acquire),
        true);
    EXPECT_EQ (readiness.error, Error::stateTransitionRejected);

    facts = baseline;
    facts.recordNodes.clear();
    facts.status = deriveAcquisitionRecordingStatus (
        false,
        {});
    readiness = evaluateAcquisitionRecordingReadiness (
        facts,
        request (Mode::record),
        false);
    EXPECT_EQ (readiness.error, Error::recordNodeRequired);

    facts = baseline;
    facts.recordNodes[0].recordingPathValid = false;
    readiness = evaluateAcquisitionRecordingReadiness (
        facts,
        request (Mode::record),
        false);
    EXPECT_EQ (readiness.error, Error::invalidRecordingPath);

    facts = baseline;
    facts.recordNodes[0].synchronized = false;
    readiness = evaluateAcquisitionRecordingReadiness (
        facts,
        request (Mode::record),
        false);
    EXPECT_EQ (
        readiness.error,
        Error::unsynchronizedConfirmationRequired);
    readiness = evaluateAcquisitionRecordingReadiness (
        facts,
        request (Mode::record, true),
        false);
    EXPECT_FALSE (readiness.error.has_value());
    EXPECT_TRUE (
        readiness.unsynchronizedConfirmationConsumed);

    facts = baseline;
    facts.audioDeviceAvailable = false;
    readiness = evaluateAcquisitionRecordingReadiness (
        facts,
        request (Mode::acquire),
        false);
    EXPECT_EQ (readiness.error, Error::audioDeviceUnavailable);

    facts = baseline;
    facts.audioSampleRate = 44099.0;
    readiness = evaluateAcquisitionRecordingReadiness (
        facts,
        request (Mode::acquire),
        false);
    EXPECT_EQ (
        readiness.error,
        Error::audioSampleRateTooLow);

    facts = baseline;
    facts.audioSampleRate =
        std::numeric_limits<double>::quiet_NaN();
    readiness = evaluateAcquisitionRecordingReadiness (
        facts,
        request (Mode::acquire),
        false);
    EXPECT_EQ (
        readiness.error,
        Error::audioSampleRateTooLow);

    facts.audioSampleRate =
        std::numeric_limits<double>::infinity();
    readiness = evaluateAcquisitionRecordingReadiness (
        facts,
        request (Mode::acquire),
        false);
    EXPECT_EQ (
        readiness.error,
        Error::audioSampleRateTooLow);

    facts = baseline;
    facts.processorGraphReady = false;
    readiness = evaluateAcquisitionRecordingReadiness (
        facts,
        request (Mode::acquire),
        false);
    EXPECT_EQ (
        readiness.error,
        Error::processorGraphNotReady);

    EXPECT_EQ (owner.startAcquisitionCount, 0);
    EXPECT_EQ (owner.startRecordingCount, 0);
    EXPECT_EQ (owner.stopAcquisitionCount, 0);
    EXPECT_EQ (owner.stopRecordingCount, 0);
}

TEST (AcquisitionRecordingReadinessTests,
      ChecksAcquisitionPreconditionsOnlyWhenCallbacksMustStart)
{
    FakeOwner owner;
    owner.callbacksActive = true;
    owner.audioDeviceAvailable = false;
    owner.audioSampleRate = 1.0;
    owner.processorGraphReady = false;
    owner.nodes = { node (1) };

    auto readiness = evaluateAcquisitionRecordingReadiness (
        owner.read(),
        request (Mode::record),
        false);
    EXPECT_FALSE (readiness.error.has_value());

    owner.nodes[0].recordingActive = true;
    owner.nodes[0].writerThreadRunning = true;
    readiness = evaluateAcquisitionRecordingReadiness (
        owner.read(),
        request (Mode::record),
        false);
    EXPECT_FALSE (readiness.error.has_value());
    EXPECT_FALSE (
        readiness.unsynchronizedConfirmationConsumed);
}

TEST (AcquisitionRecordingControlTests,
      PublishesEveryFrozenErrorIdentifierExactly)
{
    const std::vector<std::pair<Error, const char*>> expected {
        { Error::recordNodeRequired,
          "record_node_required" },
        { Error::invalidRecordingPath,
          "invalid_recording_path" },
        { Error::unsynchronizedConfirmationRequired,
          "unsynchronized_confirmation_required" },
        { Error::audioDeviceUnavailable,
          "audio_device_unavailable" },
        { Error::audioSampleRateTooLow,
          "audio_sample_rate_too_low" },
        { Error::processorGraphNotReady,
          "processor_graph_not_ready" },
        { Error::stateTransitionRejected,
          "state_transition_rejected" },
        { Error::recordingDirectoryCreateFailed,
          "recording_directory_create_failed" },
        { Error::recordingStartFailed,
          "recording_start_failed" },
        { Error::rollbackFailed,
          "rollback_failed" },
        { Error::inconsistentState,
          "inconsistent_state" },
        { Error::operationFailed,
          "operation_failed" }
    };

    for (const auto& [error, code] : expected)
    {
        EXPECT_STREQ (
            acquisitionRecordingControlErrorCode (
                error),
            code);
    }
}

TEST (AcquisitionRecordingControlTests,
      ImplementsAllIdempotentAndNonRecordingTransitions)
{
    AcquisitionRecordingControl control;

    FakeOwner idle;
    idle.nodes = { node (1) };
    auto actions = idle.actions();
    expectSuccess (
        control.applyStatusRequest (
            request (Mode::idle),
            actions),
        Mode::idle,
        false);

    actions = idle.actions();
    expectSuccess (
        control.applyStatusRequest (
            request (Mode::acquire),
            actions),
        Mode::acquire,
        true);
    EXPECT_EQ (idle.startAcquisitionCount, 1);

    actions = idle.actions();
    expectSuccess (
        control.applyStatusRequest (
            request (Mode::acquire),
            actions),
        Mode::acquire,
        false);

    actions = idle.actions();
    expectSuccess (
        control.applyStatusRequest (
            request (Mode::idle),
            actions),
        Mode::idle,
        true);
    EXPECT_EQ (idle.stopAcquisitionCount, 1);
}

TEST (AcquisitionRecordingControlTests,
      ImplementsBothRecordStartsAndRecordIdempotence)
{
    AcquisitionRecordingControl control;

    FakeOwner fromIdle;
    fromIdle.nodes = { node (1), node (2) };
    auto actions = fromIdle.actions();
    expectSuccess (
        control.applyStatusRequest (
            request (Mode::record),
            actions),
        Mode::record,
        true);
    EXPECT_EQ (
        fromIdle.actionOrder,
        (std::vector<std::string> {
            "start_acquisition",
            "start_recording"
        }));

    actions = fromIdle.actions();
    expectSuccess (
        control.applyStatusRequest (
            request (Mode::record, true),
            actions),
        Mode::record,
        false);
    EXPECT_EQ (fromIdle.startAcquisitionCount, 1);
    EXPECT_EQ (fromIdle.startRecordingCount, 1);
    EXPECT_FALSE (
        fromIdle.lastConfirmation);

    FakeOwner fromAcquire;
    fromAcquire.callbacksActive = true;
    fromAcquire.nodes = { node (3) };
    actions = fromAcquire.actions();
    expectSuccess (
        control.applyStatusRequest (
            request (Mode::record),
            actions),
        Mode::record,
        true);
    EXPECT_EQ (fromAcquire.startAcquisitionCount, 0);
    EXPECT_EQ (fromAcquire.startRecordingCount, 1);
}

TEST (AcquisitionRecordingControlTests,
      StopsRecordingBeforeCallbacksAndCanPreserveAcquisition)
{
    AcquisitionRecordingControl control;

    FakeOwner toIdle;
    toIdle.callbacksActive = true;
    toIdle.nodes = { node (1, true, true) };
    auto actions = toIdle.actions();
    expectSuccess (
        control.applyStatusRequest (
            request (Mode::idle),
            actions),
        Mode::idle,
        true);
    EXPECT_EQ (
        toIdle.actionOrder,
        (std::vector<std::string> {
            "stop_recording",
            "stop_acquisition"
        }));

    FakeOwner toAcquire;
    toAcquire.callbacksActive = true;
    toAcquire.nodes = { node (2, true, true) };
    actions = toAcquire.actions();
    expectSuccess (
        control.applyStatusRequest (
            request (Mode::acquire),
            actions),
        Mode::acquire,
        true);
    EXPECT_EQ (toAcquire.stopRecordingCount, 1);
    EXPECT_EQ (toAcquire.stopAcquisitionCount, 0);
    EXPECT_TRUE (toAcquire.callbacksActive);
}

TEST (AcquisitionRecordingControlTests,
      RejectsEveryReadinessFailureWithoutMutation)
{
    const auto run =
        [] (FakeOwner& owner,
            Mode target,
            Error error,
            bool confirmation = false)
        {
            AcquisitionRecordingControl control;
            auto actions = owner.actions();
            const auto result =
                control.applyStatusRequest (
                    request (target, confirmation),
                    actions);
            expectError (result, target, error);
            EXPECT_EQ (owner.startAcquisitionCount, 0);
            EXPECT_EQ (owner.startRecordingCount, 0);
            EXPECT_EQ (owner.stopAcquisitionCount, 0);
            EXPECT_EQ (owner.stopRecordingCount, 0);
        };

    FakeOwner noNode;
    run (noNode, Mode::record, Error::recordNodeRequired);

    FakeOwner invalidPathFromIdle;
    invalidPathFromIdle.nodes = {
        node (1, false, false, false)
    };
    run (
        invalidPathFromIdle,
        Mode::record,
        Error::invalidRecordingPath);

    FakeOwner invalidPathFromAcquire = invalidPathFromIdle;
    invalidPathFromAcquire.callbacksActive = true;
    run (
        invalidPathFromAcquire,
        Mode::record,
        Error::invalidRecordingPath);

    FakeOwner noDevice;
    noDevice.audioDeviceAvailable = false;
    run (
        noDevice,
        Mode::acquire,
        Error::audioDeviceUnavailable);

    FakeOwner lowRate;
    lowRate.audioSampleRate = 44099.999;
    run (
        lowRate,
        Mode::acquire,
        Error::audioSampleRateTooLow);

    FakeOwner graphNotReady;
    graphNotReady.processorGraphReady = false;
    run (
        graphNotReady,
        Mode::acquire,
        Error::processorGraphNotReady);

    FakeOwner unsynchronized;
    unsynchronized.nodes = {
        node (1, false, false, true, false)
    };
    run (
        unsynchronized,
        Mode::record,
        Error::unsynchronizedConfirmationRequired);
    run (
        unsynchronized,
        Mode::record,
        Error::unsynchronizedConfirmationRequired,
        false);
}

TEST (AcquisitionRecordingControlTests,
      ConfirmationIsConsumedExactlyOnceAndBypassesNothingElse)
{
    AcquisitionRecordingControl control;
    FakeOwner owner;
    owner.nodes = {
        node (1, false, false, true, false)
    };

    auto actions = owner.actions();
    auto result = control.applyStatusRequest (
        request (Mode::record, true),
        actions);
    expectSuccess (result, Mode::record, true, true);
    EXPECT_TRUE (owner.lastConfirmation);
    EXPECT_EQ (owner.startRecordingCount, 1);

    FakeOwner invalidPath;
    invalidPath.nodes = {
        node (2, false, false, false, false)
    };
    actions = invalidPath.actions();
    result = control.applyStatusRequest (
        request (Mode::record, true),
        actions);
    expectError (
        result,
        Mode::record,
        Error::invalidRecordingPath);
    EXPECT_EQ (invalidPath.startRecordingCount, 0);
    EXPECT_FALSE (result.unsynchronizedConfirmed);
}

TEST (AcquisitionRecordingControlTests,
      RollsBackOnlyRequestStartedGenerationsFromIdle)
{
    AcquisitionRecordingControl control;
    FakeOwner owner;
    owner.nodes = { node (11), node (12) };
    owner.generationsStartedByRecording = { 11, 12 };
    owner.recordingStartError = Error::recordingStartFailed;

    auto actions = owner.actions();
    const auto result = control.applyStatusRequest (
        request (Mode::record),
        actions);

    expectError (
        result,
        Mode::record,
        Error::recordingStartFailed);
    ASSERT_TRUE (result.achieved.has_value());
    EXPECT_EQ (result.achieved->status.mode, Mode::idle);
    EXPECT_EQ (owner.rolledBackGenerations,
               (std::vector<std::uint64_t> {
                   11,
                   12
               }));
    EXPECT_EQ (
        owner.actionOrder,
        (std::vector<std::string> {
            "start_acquisition",
            "start_recording",
            "rollback_recording",
            "stop_acquisition"
        }));
}

TEST (AcquisitionRecordingControlTests,
      RollsBackOnlyRequestStartedGenerationsAndPreservesAcquire)
{
    AcquisitionRecordingControl control;
    FakeOwner owner;
    owner.callbacksActive = true;
    owner.nodes = { node (21), node (22) };
    owner.generationsStartedByRecording = { 22 };
    owner.recordingStartError =
        Error::recordingDirectoryCreateFailed;

    auto actions = owner.actions();
    const auto result = control.applyStatusRequest (
        request (Mode::record),
        actions);

    expectError (
        result,
        Mode::record,
        Error::recordingDirectoryCreateFailed);
    ASSERT_TRUE (result.achieved.has_value());
    EXPECT_EQ (result.achieved->status.mode, Mode::acquire);
    EXPECT_EQ (owner.rolledBackGenerations,
               (std::vector<std::uint64_t> { 22 }));
    EXPECT_EQ (owner.stopAcquisitionCount, 0);
}

TEST (AcquisitionRecordingControlTests,
      RollbackNeverTargetsReplacementOrPreExistingGeneration)
{
    AcquisitionRecordingControl control;
    FakeOwner owner;
    owner.nodes = { node (31), node (32) };
    owner.generationsStartedByRecording = {
        31,
        999
    };
    owner.recordingStartError = Error::recordingStartFailed;

    auto actions = owner.actions();
    const auto result = control.applyStatusRequest (
        request (Mode::record),
        actions);

    expectError (
        result,
        Mode::record,
        Error::recordingStartFailed);
    EXPECT_EQ (owner.rolledBackGenerations,
               (std::vector<std::uint64_t> { 31 }));

    FakeOwner externalStart;
    externalStart.callbacksActive = true;
    externalStart.nodes = { node (33) };
    actions = externalStart.actions();
    actions.startRecording =
        [&externalStart] (
            bool,
            const AcquisitionRecordingRequestOwnershipReporter&)
        {
            ++externalStart.startRecordingCount;
            externalStart.nodes[0].recordingActive = true;
            externalStart.nodes[0].writerThreadRunning = true;
            return AcquisitionRecordingStartResult {
                {},
                Error::recordingStartFailed
            };
        };
    const auto externalResult =
        control.applyStatusRequest (
            request (Mode::record),
            actions);
    expectError (
        externalResult,
        Mode::record,
        Error::rollbackFailed);
    EXPECT_EQ (externalStart.rollbackCount, 0);
}

TEST (AcquisitionRecordingControlTests,
      RollbackNeverInfersCallbackOwnershipFromReturnOrState)
{
    AcquisitionRecordingControl control;
    FakeOwner owner;
    owner.nodes = { node (34) };
    owner.generationsStartedByRecording = { 34 };
    owner.recordingStartError =
        Error::recordingStartFailed;
    auto actions = owner.actions();
    actions.startAcquisition =
        [&owner] (
            const AcquisitionRecordingRequestOwnershipReporter&)
        {
            ++owner.startAcquisitionCount;
            owner.callbacksActive = true;
            return true;
        };

    const auto result = control.applyStatusRequest (
        request (Mode::record),
        actions);

    expectError (
        result,
        Mode::record,
        Error::rollbackFailed);
    EXPECT_EQ (owner.stopAcquisitionCount, 0);
    EXPECT_TRUE (owner.callbacksActive);
}

TEST (AcquisitionRecordingControlTests,
      ReportsRollbackFailureOrRawInconsistentReadback)
{
    AcquisitionRecordingControl control;

    FakeOwner rollbackFailed;
    rollbackFailed.nodes = { node (41) };
    rollbackFailed.generationsStartedByRecording = { 41 };
    rollbackFailed.recordingStartError =
        Error::recordingStartFailed;
    rollbackFailed.rollbackSucceeds = false;
    auto actions = rollbackFailed.actions();
    auto result = control.applyStatusRequest (
        request (Mode::record),
        actions);
    expectError (
        result,
        Mode::record,
        Error::inconsistentState);
    ASSERT_TRUE (result.achieved.has_value());
    EXPECT_FALSE (
        result.achieved->status.recordingConsistent);

    FakeOwner wrongTerminal;
    wrongTerminal.nodes = { node (42) };
    wrongTerminal.generationsStartedByRecording = { 42 };
    wrongTerminal.recordingStartError =
        Error::recordingStartFailed;
    wrongTerminal.stopAcquisitionSucceeds = false;
    actions = wrongTerminal.actions();
    result = control.applyStatusRequest (
        request (Mode::record),
        actions);
    expectError (
        result,
        Mode::record,
        Error::rollbackFailed);
    ASSERT_TRUE (result.achieved.has_value());
    EXPECT_EQ (
        result.achieved->status.mode,
        Mode::acquire);
}

TEST (AcquisitionRecordingControlTests,
      FinalMismatchAndPreExistingInconsistencyCannotSucceed)
{
    AcquisitionRecordingControl control;

    FakeOwner mismatch;
    mismatch.nodes = { node (51) };
    mismatch.generationsStartedByRecording = {};
    mismatch.recordingStartError = std::nullopt;
    mismatch.nodes.clear();
    mismatch.nodes.push_back (node (51));
    auto actions = mismatch.actions();
    actions.startRecording =
        [&mismatch] (
            bool,
            const AcquisitionRecordingRequestOwnershipReporter&)
        {
            ++mismatch.startRecordingCount;
            return AcquisitionRecordingStartResult {
                {},
                std::nullopt
            };
        };
    auto result = control.applyStatusRequest (
        request (Mode::record),
        actions);
    expectError (
        result,
        Mode::record,
        Error::recordingStartFailed);

    FakeOwner inconsistent;
    inconsistent.callbacksActive = false;
    inconsistent.nodes = { node (52, true, true) };
    actions = inconsistent.actions();
    result = control.applyStatusRequest (
        request (Mode::idle),
        actions);
    expectError (
        result,
        Mode::idle,
        Error::inconsistentState);
    EXPECT_TRUE (inconsistent.actionOrder.empty());
}

TEST (AcquisitionRecordingControlTests,
      ActionRejectionReturnsFreshStateAndNeverClaimsSuccess)
{
    AcquisitionRecordingControl control;

    FakeOwner startRejected;
    startRejected.nodes = { node (53) };
    startRejected.startAcquisitionSucceeds = false;
    auto actions = startRejected.actions();
    auto result = control.applyStatusRequest (
        request (Mode::acquire),
        actions);
    expectError (
        result,
        Mode::acquire,
        Error::stateTransitionRejected);
    ASSERT_TRUE (result.achieved.has_value());
    EXPECT_EQ (result.achieved->status.mode, Mode::idle);

    FakeOwner stopRejected;
    stopRejected.callbacksActive = true;
    stopRejected.nodes = { node (54, true, true) };
    stopRejected.stopRecordingSucceeds = false;
    actions = stopRejected.actions();
    result = control.applyStatusRequest (
        request (Mode::idle),
        actions);
    expectError (
        result,
        Mode::idle,
        Error::stateTransitionRejected);
    ASSERT_TRUE (result.achieved.has_value());
    EXPECT_EQ (result.achieved->status.mode, Mode::record);
    EXPECT_EQ (stopRejected.stopAcquisitionCount, 0);
}

TEST (AcquisitionRecordingControlTests,
      RollsBackOwnedCallbacksWhenAcquirePostReadbackFails)
{
    AcquisitionRecordingControl control;
    FakeOwner owner;
    owner.nodes = { node (55) };
    owner.readNumberToThrow = 2;

    auto actions = owner.actions();
    const auto result = control.applyStatusRequest (
        request (Mode::acquire),
        actions);

    expectError (
        result,
        Mode::acquire,
        Error::operationFailed);
    ASSERT_TRUE (result.achieved.has_value());
    EXPECT_EQ (result.achieved->status.mode, Mode::idle);
    EXPECT_EQ (owner.startAcquisitionCount, 1);
    EXPECT_EQ (owner.stopAcquisitionCount, 1);
}

TEST (AcquisitionRecordingControlTests,
      RecordIntermediateReadbackFailureRollsBackBeforeNodeStart)
{
    AcquisitionRecordingControl control;
    FakeOwner owner;
    owner.nodes = { node (59) };
    owner.readNumberToThrow = 2;

    auto actions = owner.actions();
    const auto result = control.applyStatusRequest (
        request (Mode::record),
        actions);

    expectError (
        result,
        Mode::record,
        Error::operationFailed);
    ASSERT_TRUE (result.achieved.has_value());
    EXPECT_EQ (result.achieved->status.mode, Mode::idle);
    EXPECT_EQ (owner.stopAcquisitionCount, 1);
    EXPECT_EQ (owner.startRecordingCount, 0);
}

TEST (AcquisitionRecordingControlTests,
      InvalidPostActionCopiedFactsAreInconsistent)
{
    AcquisitionRecordingControl control;
    FakeOwner owner;
    owner.nodes = { node (56) };
    auto actions = owner.actions();
    actions.startAcquisition =
        [&owner] (
            const AcquisitionRecordingRequestOwnershipReporter&
                ownership)
        {
            ++owner.startAcquisitionCount;
            owner.callbacksActive = true;
            owner.nodes[0].generation = 0;
            if (ownership.acquisitionStarted)
                ownership.acquisitionStarted();
            return true;
        };

    const auto result = control.applyStatusRequest (
        request (Mode::acquire),
        actions);

    expectError (
        result,
        Mode::acquire,
        Error::inconsistentState);
    ASSERT_TRUE (result.achieved.has_value());
    EXPECT_EQ (owner.stopAcquisitionCount, 1);
}

TEST (AcquisitionRecordingControlTests,
      InvalidIntermediateFactsPreventTheNextMutation)
{
    AcquisitionRecordingControl control;

    FakeOwner startInvalid;
    startInvalid.nodes = { node (57) };
    auto actions = startInvalid.actions();
    actions.startAcquisition =
        [&startInvalid] (
            const AcquisitionRecordingRequestOwnershipReporter&
                ownership)
        {
            ++startInvalid.startAcquisitionCount;
            startInvalid.callbacksActive = true;
            startInvalid.nodes[0].generation = 0;
            if (ownership.acquisitionStarted)
                ownership.acquisitionStarted();
            return true;
        };
    auto result = control.applyStatusRequest (
        request (Mode::record),
        actions);
    expectError (
        result,
        Mode::record,
        Error::inconsistentState);
    EXPECT_EQ (startInvalid.startRecordingCount, 0);

    FakeOwner stopInvalid;
    stopInvalid.callbacksActive = true;
    stopInvalid.nodes = { node (58, true, true) };
    actions = stopInvalid.actions();
    actions.stopRecording =
        [&stopInvalid]
        {
            ++stopInvalid.stopRecordingCount;
            stopInvalid.nodes[0].recordingActive = false;
            stopInvalid.nodes[0].writerThreadRunning = false;
            stopInvalid.nodes[0].generation = 0;
            return true;
        };
    result = control.applyStatusRequest (
        request (Mode::idle),
        actions);
    expectError (
        result,
        Mode::idle,
        Error::inconsistentState);
    EXPECT_EQ (stopInvalid.stopAcquisitionCount, 0);
}

TEST (AcquisitionRecordingControlTests,
      ClearsConfirmationAfterSuccessRefusalRollbackAndException)
{
    AcquisitionRecordingControl control;

    const auto verifyNoLeakedConfirmation =
        [&control] (FakeOwner& owner)
        {
            owner.callbacksActive = true;
            for (auto& current : owner.nodes)
            {
                current.recordingActive = false;
                current.writerThreadRunning = false;
                current.recordingPathValid = true;
                current.synchronized = false;
            }
            owner.recordingStartError.reset();
            owner.generationsStartedByRecording.clear();
            owner.throwWhileStartingRecording = false;
            auto actions = owner.actions();
            const auto next = control.applyStatusRequest (
                request (Mode::record),
                actions);
            expectError (
                next,
                Mode::record,
                Error::unsynchronizedConfirmationRequired);
        };

    FakeOwner success;
    success.nodes = {
        node (61, false, false, true, false)
    };
    auto actions = success.actions();
    auto result = control.applyStatusRequest (
        request (Mode::record, true),
        actions);
    expectSuccess (result, Mode::record, true, true);
    actions = success.actions();
    result = control.applyStatusRequest (
        request (Mode::acquire),
        actions);
    expectSuccess (result, Mode::acquire, true);
    verifyNoLeakedConfirmation (success);

    FakeOwner refusal;
    refusal.nodes = {
        node (62, false, false, false, false)
    };
    actions = refusal.actions();
    result = control.applyStatusRequest (
        request (Mode::record, true),
        actions);
    expectError (
        result,
        Mode::record,
        Error::invalidRecordingPath);
    verifyNoLeakedConfirmation (refusal);

    FakeOwner rollback;
    rollback.callbacksActive = true;
    rollback.nodes = {
        node (63, false, false, true, false)
    };
    rollback.generationsStartedByRecording = { 63 };
    rollback.recordingStartError =
        Error::recordingStartFailed;
    actions = rollback.actions();
    result = control.applyStatusRequest (
        request (Mode::record, true),
        actions);
    expectError (
        result,
        Mode::record,
        Error::recordingStartFailed);
    verifyNoLeakedConfirmation (rollback);

    FakeOwner exception;
    exception.callbacksActive = true;
    exception.nodes = {
        node (64, false, false, true, false)
    };
    exception.throwWhileStartingRecording = true;
    exception.generationsStartedByRecording = { 64 };
    actions = exception.actions();
    result = control.applyStatusRequest (
        request (Mode::record, true),
        actions);
    expectError (
        result,
        Mode::record,
        Error::operationFailed);
    verifyNoLeakedConfirmation (exception);
}

TEST (AcquisitionRecordingControlTests,
      SerializesConflictingRequestsWithOneInFlightOwner)
{
    AcquisitionRecordingControl control;
    FakeOwner owner;
    std::promise<void> actionStarted;
    std::promise<void> releaseAction;
    auto releaseFuture =
        releaseAction.get_future().share();
    owner.beforeStartAcquisition =
        [&]
        {
            actionStarted.set_value();
            releaseFuture.wait();
        };

    Result first;
    auto firstActions = owner.actions();
    std::thread firstThread (
        [&]
        {
            first = control.applyStatusRequest (
                request (Mode::acquire),
                firstActions);
        });

    const auto started =
        actionStarted.get_future().wait_for (
            std::chrono::seconds (2));
    std::optional<Result> second;
    if (started == std::future_status::ready)
    {
        auto secondActions = owner.actions();
        second = control.applyStatusRequest (
            request (Mode::idle),
            secondActions);
    }
    releaseAction.set_value();
    firstThread.join();

    ASSERT_EQ (started, std::future_status::ready);
    ASSERT_TRUE (second.has_value());
    expectError (
        *second,
        Mode::idle,
        Error::stateTransitionRejected);
    EXPECT_FALSE (second->achieved.has_value());
    EXPECT_EQ (owner.stopAcquisitionCount, 0);
    expectSuccess (first, Mode::acquire, true);
    EXPECT_EQ (owner.startAcquisitionCount, 1);
}
