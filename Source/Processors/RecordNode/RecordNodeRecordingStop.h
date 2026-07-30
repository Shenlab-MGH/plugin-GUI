/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2024 Open Ephys

    ------------------------------------------------------------------
*/

#ifndef RECORD_NODE_RECORDING_STOP_H
#define RECORD_NODE_RECORDING_STOP_H

#include "RecordThreadCleanStop.h"

#include <chrono>
#include <optional>

/** Stable outcome categories for an agent-safe, non-forcing writer stop. */
enum class RecordNodeRecordingStopError
{
    writerUnavailable,
    writerExitedUncleanly,
    writerTimedOut
};

/** A value-only request token: it deliberately retains no node or writer. */
struct RecordNodeRecordingStopRequest
{
    int nodeId = 0;
};

/** Result of phase one: stop accepting new recording data and signal writer. */
struct RecordNodeRecordingStopRequestResult
{
    int nodeId = 0;
    bool writerAvailable = false;
    std::optional<RecordNodeRecordingStopError> error;
};

/** Result of phase two: wait for cooperative writer cleanup until deadline. */
struct RecordNodeRecordingStopResult
{
    int nodeId = 0;
    bool writerThreadRunning = false;
    std::optional<RecordNodeRecordingStopError> error;
};

template <typename Owner>
RecordNodeRecordingStopRequestResult
requestRecordNodeRecordingStop (
    const RecordNodeRecordingStopRequest& request,
    Owner& owner)
{
    RecordNodeRecordingStopRequestResult result {
        request.nodeId
    };

    if (owner.isRecordingActive())
    {
        owner.markRecordingInactive();
        owner.markHasRecorded();
        owner.advanceRecordingIndex();
    }

    result.writerAvailable = owner.writerAvailable();
    if (! result.writerAvailable)
    {
        result.error =
            RecordNodeRecordingStopError::
                writerUnavailable;
        return result;
    }

    owner.requestWriterCleanStop();
    return result;
}

template <typename Owner>
RecordNodeRecordingStopRequestResult
requestRecordNodeRecordingStopFromOwner (Owner& owner)
{
    const RecordNodeRecordingStopRequest request {
        owner.nodeId()
    };
    return requestRecordNodeRecordingStop (
        request,
        owner);
}

template <typename Owner>
RecordNodeRecordingStopResult
waitForRecordNodeRecordingStopUntil (
    const RecordNodeRecordingStopRequestResult& request,
    Owner& owner,
    std::chrono::steady_clock::time_point deadline)
{
    RecordNodeRecordingStopResult result {
        request.nodeId
    };

    if (! request.writerAvailable)
    {
        result.error =
            RecordNodeRecordingStopError::
                writerUnavailable;
        return result;
    }

    switch (owner.waitForWriterCleanStopUntil (deadline))
    {
        case RecordThreadCleanStopOutcome::alreadyStoppedClean:
        case RecordThreadCleanStopOutcome::cleanExit:
            return result;

        case RecordThreadCleanStopOutcome::alreadyStoppedUnclean:
        case RecordThreadCleanStopOutcome::uncleanExit:
            result.error =
                RecordNodeRecordingStopError::
                    writerExitedUncleanly;
            return result;

        case RecordThreadCleanStopOutcome::timedOut:
            result.writerThreadRunning = true;
            result.error =
                RecordNodeRecordingStopError::
                    writerTimedOut;
            return result;
    }

    result.error =
        RecordNodeRecordingStopError::
            writerExitedUncleanly;
    return result;
}

#endif
