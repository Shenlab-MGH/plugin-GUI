/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2024 Open Ephys

    ------------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    ------------------------------------------------------------------
*/

#include "AcquisitionRecordingControl.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace
{
using Error = AcquisitionRecordingControlError;
using Mode = AcquisitionRecordingMode;
using Snapshot = AcquisitionRecordingControlSnapshot;

bool statusesMatch (const AcquisitionRecordingStatus& first,
                    const AcquisitionRecordingStatus& second)
{
    return first.mode == second.mode
           && first.acquisitionActive == second.acquisitionActive
           && first.recordingActive == second.recordingActive
           && first.recordNodeCount == second.recordNodeCount
           && first.activeRecordNodeCount
                  == second.activeRecordNodeCount
           && first.writerThreadRunningCount
                  == second.writerThreadRunningCount
           && first.recordingConsistent
                  == second.recordingConsistent;
}

bool hasValidCopiedNodeFacts (const Snapshot& snapshot)
{
    if (snapshot.recordNodes.size()
        != snapshot.status.recordNodeCount)
        return false;

    std::set<std::uint64_t> generations;
    std::vector<RecordNodeRuntimeState> runtimeNodes;
    runtimeNodes.reserve (snapshot.recordNodes.size());

    for (const auto& node : snapshot.recordNodes)
    {
        if (node.generation == 0
            || ! generations.insert (node.generation).second)
            return false;

        runtimeNodes.push_back ({
            node.recordingActive,
            node.writerThreadRunning
        });
    }

    return statusesMatch (
        snapshot.status,
        deriveAcquisitionRecordingStatus (
            snapshot.status.acquisitionActive,
            runtimeNodes));
}

std::optional<Snapshot> readSafely (
    AcquisitionRecordingOwnerActions& owner)
{
    if (! owner.readback)
        return std::nullopt;

    try
    {
        return owner.readback();
    }
    catch (...)
    {
        return std::nullopt;
    }
}

AcquisitionRecordingControlResult makeError (
    Mode requestedMode,
    Error error,
    std::optional<Snapshot> achieved = std::nullopt)
{
    return {
        requestedMode,
        std::move (achieved),
        false,
        false,
        error
    };
}

AcquisitionRecordingControlResult makeSuccess (
    Mode requestedMode,
    Snapshot achieved,
    bool changed,
    bool confirmationConsumed)
{
    return {
        requestedMode,
        std::move (achieved),
        changed,
        confirmationConsumed,
        std::nullopt
    };
}

Error finalMismatchError (const Snapshot& snapshot)
{
    return snapshot.status.recordingConsistent
                   && snapshot.status.mode.has_value()
                   && hasValidCopiedNodeFacts (snapshot)
               ? Error::stateTransitionRejected
               : Error::inconsistentState;
}

bool isExpectedRecordingStartError (Error error)
{
    return error == Error::recordingDirectoryCreateFailed
           || error == Error::recordingStartFailed;
}

std::vector<std::uint64_t> intersectGenerations (
    const std::vector<std::uint64_t>& candidates,
    const std::set<std::uint64_t>& requestEligible)
{
    std::vector<std::uint64_t> result;
    for (auto generation : candidates)
    {
        if (requestEligible.count (generation) != 0
            && std::find (
                   result.begin(),
                   result.end(),
                   generation)
                   == result.end())
            result.push_back (generation);
    }
    return result;
}
} // namespace

const char* acquisitionRecordingControlErrorCode (Error error)
{
    switch (error)
    {
        case Error::recordNodeRequired:
            return "record_node_required";
        case Error::invalidRecordingPath:
            return "invalid_recording_path";
        case Error::unsynchronizedConfirmationRequired:
            return "unsynchronized_confirmation_required";
        case Error::audioDeviceUnavailable:
            return "audio_device_unavailable";
        case Error::audioSampleRateTooLow:
            return "audio_sample_rate_too_low";
        case Error::processorGraphNotReady:
            return "processor_graph_not_ready";
        case Error::stateTransitionRejected:
            return "state_transition_rejected";
        case Error::recordingDirectoryCreateFailed:
            return "recording_directory_create_failed";
        case Error::recordingStartFailed:
            return "recording_start_failed";
        case Error::rollbackFailed:
            return "rollback_failed";
        case Error::inconsistentState:
            return "inconsistent_state";
        case Error::operationFailed:
            return "operation_failed";
    }

    return "operation_failed";
}

AcquisitionRecordingReadinessResult
evaluateAcquisitionRecordingReadiness (
    const Snapshot& snapshot,
    const StatusRequest& request,
    bool transitionInFlight)
{
    if (! snapshot.status.recordingConsistent
        || ! snapshot.status.mode.has_value()
        || ! hasValidCopiedNodeFacts (snapshot))
        return { Error::inconsistentState, false };

    if (transitionInFlight
        || (request.mode != Mode::record
            && request.confirmUnsynchronized))
        return { Error::stateTransitionRejected, false };

    const auto currentMode = *snapshot.status.mode;
    if (currentMode == request.mode)
        return {};

    if (request.mode == Mode::record)
    {
        if (snapshot.recordNodes.empty())
            return { Error::recordNodeRequired, false };

        for (const auto& node : snapshot.recordNodes)
        {
            if (! node.recordingPathValid)
                return { Error::invalidRecordingPath, false };
        }

        const auto unsynchronized =
            std::any_of (
                snapshot.recordNodes.begin(),
                snapshot.recordNodes.end(),
                [] (const auto& node)
                {
                    return ! node.synchronized;
                });

        if (unsynchronized
            && ! request.confirmUnsynchronized)
        {
            return {
                Error::unsynchronizedConfirmationRequired,
                false
            };
        }

        if (currentMode == Mode::acquire)
            return { std::nullopt, unsynchronized };
    }

    const auto mustStartAcquisition =
        currentMode == Mode::idle
        && (request.mode == Mode::acquire
            || request.mode == Mode::record);

    if (mustStartAcquisition)
    {
        if (! snapshot.audioDeviceAvailable)
            return { Error::audioDeviceUnavailable, false };

        if (! std::isfinite (snapshot.audioSampleRate)
            || snapshot.audioSampleRate < 44100.0)
            return { Error::audioSampleRateTooLow, false };

        if (! snapshot.processorGraphReady)
            return { Error::processorGraphNotReady, false };
    }

    const auto confirmationConsumed =
        request.mode == Mode::record
        && std::any_of (
            snapshot.recordNodes.begin(),
            snapshot.recordNodes.end(),
            [] (const auto& node)
            {
                return ! node.synchronized;
            });

    return { std::nullopt, confirmationConsumed };
}

AcquisitionRecordingControlResult
AcquisitionRecordingControl::applyStatusRequest (
    StatusRequest request,
    AcquisitionRecordingOwnerActions& owner)
{
    bool expected = false;
    if (! transitionInFlight.compare_exchange_strong (
            expected,
            true))
    {
        return makeError (
            request.mode,
            Error::stateTransitionRejected);
    }

    struct InFlightReset
    {
        explicit InFlightReset (std::atomic<bool>& flag)
            : flag (flag)
        {
        }

        ~InFlightReset()
        {
            flag.store (false);
        }

        std::atomic<bool>& flag;
    } reset (transitionInFlight);

    auto initial = readSafely (owner);
    if (! initial.has_value())
        return makeError (
            request.mode,
            Error::operationFailed);

    const auto readiness =
        evaluateAcquisitionRecordingReadiness (
            *initial,
            request,
            false);
    if (readiness.error.has_value())
    {
        return makeError (
            request.mode,
            *readiness.error,
            std::move (initial));
    }

    const auto priorMode = *initial->status.mode;
    if (priorMode == request.mode)
    {
        return makeSuccess (
            request.mode,
            std::move (*initial),
            false,
            false);
    }

    const auto needsStartAcquisition =
        priorMode == Mode::idle
        && request.mode != Mode::idle;
    const auto needsStopAcquisition =
        request.mode == Mode::idle
        && priorMode != Mode::idle;
    const auto needsStartRecording =
        request.mode == Mode::record;
    const auto needsStopRecording =
        priorMode == Mode::record
        && request.mode != Mode::record;

    if ((needsStartAcquisition
         && ! owner.startAcquisition)
        || (needsStopAcquisition
            && ! owner.stopAcquisition)
        || (needsStartRecording
            && ! owner.startRecording)
        || (needsStopRecording
            && ! owner.stopRecording))
    {
        return makeError (
            request.mode,
            Error::operationFailed,
            std::move (initial));
    }

    const auto readAndVerify =
        [&] (Mode expectedMode)
            -> AcquisitionRecordingControlResult
        {
            auto achieved = readSafely (owner);
            if (! achieved.has_value())
            {
                return makeError (
                    request.mode,
                    Error::operationFailed);
            }

            if (! achieved->status.recordingConsistent
                || achieved->status.mode
                       != std::optional<Mode> (expectedMode)
                || ! hasValidCopiedNodeFacts (*achieved))
            {
                return makeError (
                    request.mode,
                    finalMismatchError (*achieved),
                    std::move (achieved));
            }

            return makeSuccess (
                request.mode,
                std::move (*achieved),
                true,
                false);
        };

    if (request.mode == Mode::idle)
    {
        try
        {
            if (priorMode == Mode::record)
            {
                if (! owner.stopRecording)
                {
                    return makeError (
                        request.mode,
                        Error::operationFailed,
                        readSafely (owner));
                }

                if (! owner.stopRecording())
                {
                    return makeError (
                        request.mode,
                        Error::stateTransitionRejected,
                        readSafely (owner));
                }

                const auto afterRecording =
                    readSafely (owner);
                if (! afterRecording.has_value())
                {
                    return makeError (
                        request.mode,
                        Error::operationFailed);
                }
                if (afterRecording->status.mode
                        != std::optional<Mode> (Mode::acquire)
                    || ! afterRecording->status
                            .recordingConsistent
                    || ! hasValidCopiedNodeFacts (
                        *afterRecording))
                {
                    return makeError (
                        request.mode,
                        finalMismatchError (
                            *afterRecording),
                        afterRecording);
                }
            }

            if (! owner.stopAcquisition)
            {
                return makeError (
                    request.mode,
                    Error::operationFailed,
                    readSafely (owner));
            }

            if (! owner.stopAcquisition())
            {
                return makeError (
                    request.mode,
                    Error::stateTransitionRejected,
                    readSafely (owner));
            }
        }
        catch (...)
        {
            return makeError (
                request.mode,
                Error::operationFailed,
                readSafely (owner));
        }

        return readAndVerify (Mode::idle);
    }

    if (request.mode == Mode::acquire)
    {
        if (priorMode == Mode::record)
        {
            try
            {
                if (! owner.stopRecording)
                {
                    return makeError (
                        request.mode,
                        Error::operationFailed,
                        readSafely (owner));
                }

                if (! owner.stopRecording())
                {
                    return makeError (
                        request.mode,
                        Error::stateTransitionRejected,
                        readSafely (owner));
                }
            }
            catch (...)
            {
                return makeError (
                    request.mode,
                    Error::operationFailed,
                    readSafely (owner));
            }

            return readAndVerify (Mode::acquire);
        }

        bool callbacksStartedByRequest = false;
        const AcquisitionRecordingRequestOwnershipReporter
            ownership {
                [&]
                {
                    callbacksStartedByRequest = true;
                },
                {}
            };

        const auto rollbackAcquisition =
            [&] (Error originalError)
                -> AcquisitionRecordingControlResult
            {
                if (callbacksStartedByRequest
                    && owner.stopAcquisition)
                {
                    try
                    {
                        owner.stopAcquisition();
                    }
                    catch (...)
                    {
                    }
                }

                auto achieved = readSafely (owner);
                if (! achieved.has_value())
                {
                    return makeError (
                        request.mode,
                        Error::rollbackFailed);
                }

                if (! achieved->status.recordingConsistent
                    || ! hasValidCopiedNodeFacts (*achieved))
                {
                    return makeError (
                        request.mode,
                        Error::inconsistentState,
                        std::move (achieved));
                }

                if (achieved->status.mode
                    != std::optional<Mode> (Mode::idle))
                {
                    return makeError (
                        request.mode,
                        Error::rollbackFailed,
                        std::move (achieved));
                }

                return makeError (
                    request.mode,
                    originalError,
                    std::move (achieved));
            };

        try
        {
            if (! owner.startAcquisition)
                return rollbackAcquisition (
                    Error::operationFailed);

            if (! owner.startAcquisition (ownership))
            {
                return rollbackAcquisition (
                    Error::stateTransitionRejected);
            }
        }
        catch (...)
        {
            return rollbackAcquisition (
                Error::operationFailed);
        }

        auto achieved = readSafely (owner);
        if (! achieved.has_value())
            return rollbackAcquisition (
                Error::operationFailed);

        if (! achieved->status.recordingConsistent
            || ! hasValidCopiedNodeFacts (*achieved))
            return rollbackAcquisition (
                Error::inconsistentState);

        if (achieved->status.mode
            != std::optional<Mode> (Mode::acquire))
            return rollbackAcquisition (
                Error::stateTransitionRejected);

        return makeSuccess (
            request.mode,
            std::move (*achieved),
            true,
            false);
    }

    std::set<std::uint64_t> requestEligibleGenerations;
    for (const auto& node : initial->recordNodes)
    {
        if (! node.recordingActive)
            requestEligibleGenerations.insert (
                node.generation);
    }

    bool callbacksStartedByRequest = false;
    std::vector<std::uint64_t> reportedStartedGenerations;
    const AcquisitionRecordingRequestOwnershipReporter
        ownership {
            [&]
            {
                callbacksStartedByRequest = true;
            },
            [&] (std::uint64_t generation)
            {
                if (requestEligibleGenerations.count (
                        generation)
                        != 0
                    && std::find (
                           reportedStartedGenerations.begin(),
                           reportedStartedGenerations.end(),
                           generation)
                           == reportedStartedGenerations.end())
                {
                    reportedStartedGenerations.push_back (
                        generation);
                }
            }
        };

    const auto rollback =
        [&] (Error originalError)
            -> AcquisitionRecordingControlResult
        {
            auto ownedGenerations =
                intersectGenerations (
                    reportedStartedGenerations,
                    requestEligibleGenerations);

            if (! ownedGenerations.empty()
                && owner.rollbackRecordingStarts)
            {
                try
                {
                    owner.rollbackRecordingStarts (
                        ownedGenerations);
                }
                catch (...)
                {
                }
            }

            if (callbacksStartedByRequest
                && owner.stopAcquisition)
            {
                try
                {
                    owner.stopAcquisition();
                }
                catch (...)
                {
                }
            }

            auto achieved = readSafely (owner);
            if (! achieved.has_value())
            {
                return makeError (
                    request.mode,
                    Error::rollbackFailed);
            }

            if (! achieved->status.recordingConsistent
                || ! hasValidCopiedNodeFacts (*achieved))
            {
                return makeError (
                    request.mode,
                    Error::inconsistentState,
                    std::move (achieved));
            }

            if (achieved->status.mode
                != std::optional<Mode> (priorMode))
            {
                return makeError (
                    request.mode,
                    Error::rollbackFailed,
                    std::move (achieved));
            }

            return makeError (
                request.mode,
                originalError,
                std::move (achieved));
        };

    if (priorMode == Mode::idle)
    {
        try
        {
            if (! owner.startAcquisition)
                return rollback (Error::operationFailed);

            if (! owner.startAcquisition (ownership))
            {
                return rollback (
                    Error::stateTransitionRejected);
            }
        }
        catch (...)
        {
            return rollback (Error::operationFailed);
        }

        const auto afterAcquisition =
            readSafely (owner);
        if (! afterAcquisition.has_value())
            return rollback (Error::operationFailed);

        if (! afterAcquisition->status
                 .recordingConsistent
            || ! hasValidCopiedNodeFacts (
                   *afterAcquisition))
            return rollback (Error::inconsistentState);

        if (afterAcquisition->status.mode
            != std::optional<Mode> (Mode::acquire))
        {
            return rollback (
                Error::stateTransitionRejected);
        }
    }

    try
    {
        if (! owner.startRecording)
            return rollback (Error::operationFailed);

        auto startResult = owner.startRecording (
            readiness
                .unsynchronizedConfirmationConsumed,
            ownership);
        const auto returnedGenerations =
            intersectGenerations (
                startResult.startedNodeGenerations,
                requestEligibleGenerations);
        for (auto generation : returnedGenerations)
        {
            if (std::find (
                    reportedStartedGenerations.begin(),
                    reportedStartedGenerations.end(),
                    generation)
                == reportedStartedGenerations.end())
            {
                reportedStartedGenerations.push_back (
                    generation);
            }
        }

        if (startResult.error.has_value())
        {
            return rollback (
                isExpectedRecordingStartError (
                    *startResult.error)
                    ? *startResult.error
                    : Error::operationFailed);
        }
    }
    catch (...)
    {
        return rollback (Error::operationFailed);
    }

    auto achieved = readSafely (owner);
    if (! achieved.has_value())
        return rollback (Error::operationFailed);

    if (! achieved->status.recordingConsistent
        || ! hasValidCopiedNodeFacts (*achieved))
        return rollback (Error::inconsistentState);

    if (achieved->status.mode
        != std::optional<Mode> (Mode::record))
        return rollback (Error::recordingStartFailed);

    return makeSuccess (
        request.mode,
        std::move (*achieved),
        true,
        readiness
            .unsynchronizedConfirmationConsumed);
}
