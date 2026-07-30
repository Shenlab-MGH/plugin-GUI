/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2024 Open Ephys

    ------------------------------------------------------------------
*/

#ifndef PROCESSOR_GRAPH_RECORDING_STOP_H
#define PROCESSOR_GRAPH_RECORDING_STOP_H

#include "../RecordNode/RecordNodeRecordingStop.h"

#include <chrono>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

/** Aggregate status for a graph-wide, cooperative recording stop. */
enum class ProcessorGraphRecordingStopStatus
{
    stoppedCleanly,
    stoppedWithFailure,
    timedOut
};

/** A value-only RecordNode stop request paired with its stable graph index. */
struct ProcessorGraphRecordingStopNodeRequest
{
    std::size_t graphIndex = 0;
    RecordNodeRecordingStopRequestResult request;
};

/** A complete phase-one graph stop request. It owns no RecordNode pointers. */
struct ProcessorGraphRecordingStopRequestBatch
{
    std::vector<ProcessorGraphRecordingStopNodeRequest> nodes;
};

/** Complete request and wait outcome for one stable graph index. */
struct ProcessorGraphRecordingStopNodeResult
{
    std::size_t graphIndex = 0;
    RecordNodeRecordingStopRequestResult request;
    RecordNodeRecordingStopResult completion;
};

/** Value-only result for a complete graph-wide cooperative recording stop. */
struct ProcessorGraphRecordingStopResult
{
    ProcessorGraphRecordingStopStatus status =
        ProcessorGraphRecordingStopStatus::stoppedWithFailure;
    std::optional<RecordNodeRecordingStopError> error;
    std::vector<ProcessorGraphRecordingStopNodeResult> nodes;
};

template <typename Owner>
ProcessorGraphRecordingStopRequestBatch
requestProcessorGraphRecordingStop (Owner& owner)
{
    ProcessorGraphRecordingStopRequestBatch batch;
    const auto count = owner.recordNodeCount();
    batch.nodes.reserve (count);

    for (std::size_t index = 0; index < count; ++index)
    {
        batch.nodes.push_back ({
            index,
            owner.requestRecordingStopAt (index)
        });
    }

    return batch;
}

inline void considerProcessorGraphRecordingStopError (
    const std::optional<RecordNodeRecordingStopError>& error,
    bool& hasTimeout,
    bool& hasUnavailable,
    bool& hasUnclean,
    std::optional<RecordNodeRecordingStopError>& firstUnknown)
{
    if (! error.has_value())
        return;

    switch (*error)
    {
        case RecordNodeRecordingStopError::writerTimedOut:
            hasTimeout = true;
            return;

        case RecordNodeRecordingStopError::writerUnavailable:
            hasUnavailable = true;
            return;

        case RecordNodeRecordingStopError::writerExitedUncleanly:
            hasUnclean = true;
            return;
    }

    if (! firstUnknown.has_value())
        firstUnknown = error;
}

inline ProcessorGraphRecordingStopResult
makeProcessorGraphRecordingStopResult (
    std::vector<ProcessorGraphRecordingStopNodeResult> nodes)
{
    bool hasTimeout = false;
    bool hasUnavailable = false;
    bool hasUnclean = false;
    std::optional<RecordNodeRecordingStopError> firstUnknown;

    for (const auto& node : nodes)
    {
        considerProcessorGraphRecordingStopError (
            node.request.error,
            hasTimeout,
            hasUnavailable,
            hasUnclean,
            firstUnknown);
        considerProcessorGraphRecordingStopError (
            node.completion.error,
            hasTimeout,
            hasUnavailable,
            hasUnclean,
            firstUnknown);
    }

    ProcessorGraphRecordingStopResult result;
    result.nodes = std::move (nodes);

    if (hasTimeout)
    {
        result.status = ProcessorGraphRecordingStopStatus::timedOut;
        result.error = RecordNodeRecordingStopError::writerTimedOut;
        return result;
    }

    if (hasUnavailable)
    {
        result.status = ProcessorGraphRecordingStopStatus::stoppedWithFailure;
        result.error = RecordNodeRecordingStopError::writerUnavailable;
        return result;
    }

    if (hasUnclean)
    {
        result.status = ProcessorGraphRecordingStopStatus::stoppedWithFailure;
        result.error = RecordNodeRecordingStopError::writerExitedUncleanly;
        return result;
    }

    if (firstUnknown.has_value())
    {
        result.status = ProcessorGraphRecordingStopStatus::stoppedWithFailure;
        result.error = firstUnknown;
        return result;
    }

    result.status = ProcessorGraphRecordingStopStatus::stoppedCleanly;
    return result;
}

template <typename Owner>
ProcessorGraphRecordingStopResult
waitForProcessorGraphRecordingStopUntil (
    const ProcessorGraphRecordingStopRequestBatch& batch,
    Owner& owner,
    std::chrono::steady_clock::time_point deadline)
{
    std::vector<ProcessorGraphRecordingStopNodeResult> nodes;
    nodes.reserve (batch.nodes.size());

    for (const auto& node : batch.nodes)
    {
        nodes.push_back ({
            node.graphIndex,
            node.request,
            owner.waitForRecordingStopUntilAt (
                node.graphIndex,
                node.request,
                deadline)
        });
    }

    return makeProcessorGraphRecordingStopResult (std::move (nodes));
}

template <typename Owner>
ProcessorGraphRecordingStopResult
stopProcessorGraphRecordingUntil (
    Owner& owner,
    std::chrono::steady_clock::time_point deadline)
{
    const auto batch = requestProcessorGraphRecordingStop (owner);
    return waitForProcessorGraphRecordingStopUntil (
        batch,
        owner,
        deadline);
}

#endif
