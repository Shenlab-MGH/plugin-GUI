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

#ifndef ACQUISITION_RECORDING_CONTROL_H
#define ACQUISITION_RECORDING_CONTROL_H

#include "AcquisitionRecordingRequest.h"
#include "../TestableExport.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

enum class AcquisitionRecordingControlError
{
    recordNodeRequired,
    invalidRecordingPath,
    unsynchronizedConfirmationRequired,
    audioDeviceUnavailable,
    audioSampleRateTooLow,
    processorGraphNotReady,
    stateTransitionRejected,
    recordingDirectoryCreateFailed,
    recordingStartFailed,
    rollbackFailed,
    inconsistentState,
    operationFailed
};

TESTABLE const char* acquisitionRecordingControlErrorCode (
    AcquisitionRecordingControlError error);

struct AcquisitionRecordingControlNodeState
{
    std::uint64_t generation = 0;
    bool recordingActive = false;
    bool writerThreadRunning = false;
    bool recordingPathValid = false;
    bool synchronized = false;
};

/**
    A copied controller input/readback. It retains no GUI, graph, processor,
    thread, file, parameter, or metadata owner.
*/
struct AcquisitionRecordingControlSnapshot
{
    AcquisitionRecordingStatus status;
    std::vector<AcquisitionRecordingControlNodeState> recordNodes;
    bool audioDeviceAvailable = false;
    double audioSampleRate = 0.0;
    bool processorGraphReady = false;
};

/**
    Validates that the copied per-node facts exactly reproduce the aggregate
    status and that every node generation is non-zero and unique.
*/
TESTABLE bool isValidAcquisitionRecordingControlSnapshot (
    const AcquisitionRecordingControlSnapshot& snapshot);

struct AcquisitionRecordingReadinessResult
{
    std::optional<AcquisitionRecordingControlError> error;
    bool unsynchronizedConfirmationConsumed = false;
};

TESTABLE AcquisitionRecordingReadinessResult
evaluateAcquisitionRecordingReadiness (
    const AcquisitionRecordingControlSnapshot& snapshot,
    const StatusRequest& request,
    bool transitionInFlight);

struct AcquisitionRecordingStartResult
{
    std::vector<std::uint64_t> startedNodeGenerations;
    std::optional<AcquisitionRecordingControlError> error;
};

/**
    Reports exact resources after this request starts them. The callbacks
    remain valid only for the duration of the injected start action.
*/
struct AcquisitionRecordingRequestOwnershipReporter
{
    std::function<void()> acquisitionStarted;
    std::function<void (std::uint64_t)> recordNodeStarted;
};

/**
    Message-thread owner seams. The controller invokes these synchronously and
    never retains them after applyStatusRequest returns.
*/
struct AcquisitionRecordingOwnerActions
{
    std::function<AcquisitionRecordingControlSnapshot()> readback;
    std::function<bool (
        const AcquisitionRecordingRequestOwnershipReporter&)>
        startAcquisition;
    std::function<bool()> stopAcquisition;
    std::function<AcquisitionRecordingStartResult (
        bool,
        const AcquisitionRecordingRequestOwnershipReporter&)>
        startRecording;
    std::function<bool()> stopRecording;
    std::function<bool (const std::vector<std::uint64_t>&)>
        rollbackRecordingStarts;
};

struct AcquisitionRecordingControlResult
{
    AcquisitionRecordingMode requestedMode =
        AcquisitionRecordingMode::idle;
    std::optional<AcquisitionRecordingControlSnapshot> achieved;
    bool changed = false;
    bool unsynchronizedConfirmed = false;
    std::optional<AcquisitionRecordingControlError> error;
};

/**
    Serializes one synchronous message-thread transition at a time.

    The coordinator contains no GUI behavior and stores no request-local
    confirmation or owner reference between calls.
*/
class TESTABLE AcquisitionRecordingControl
{
public:
    AcquisitionRecordingControlResult applyStatusRequest (
        StatusRequest request,
        AcquisitionRecordingOwnerActions& owner);

private:
    std::atomic<bool> transitionInFlight { false };
};

#endif
