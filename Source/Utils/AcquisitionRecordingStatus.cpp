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

#include "AcquisitionRecordingStatus.h"

#include "../Audio/AudioComponent.h"
#include "../Processors/ProcessorGraph/ProcessorGraph.h"
#include "../Processors/RecordNode/RecordNode.h"

AcquisitionRecordingStatus deriveAcquisitionRecordingStatus (
    bool callbacksAreActive,
    const std::vector<RecordNodeRuntimeState>& recordNodes)
{
    AcquisitionRecordingStatus status;
    status.acquisitionActive = callbacksAreActive;
    status.recordNodeCount = recordNodes.size();

    for (const auto& node : recordNodes)
    {
        if (node.recordingActive)
            ++status.activeRecordNodeCount;

        if (node.writerThreadRunning)
            ++status.writerThreadRunningCount;
    }

    const bool noNodesAreActive =
        status.activeRecordNodeCount == 0;
    const bool everyNodeIsActive =
        status.recordNodeCount > 0
        && status.activeRecordNodeCount == status.recordNodeCount;
    const bool everyWriterIsRunning =
        status.writerThreadRunningCount == status.recordNodeCount;

    status.recordingActive =
        callbacksAreActive
        && everyNodeIsActive
        && everyWriterIsRunning;
    status.recordingConsistent =
        noNodesAreActive || status.recordingActive;

    if (status.recordingActive)
        status.mode = AcquisitionRecordingMode::record;
    else if (noNodesAreActive)
        status.mode = callbacksAreActive
                        ? AcquisitionRecordingMode::acquire
                        : AcquisitionRecordingMode::idle;

    return status;
}

AcquisitionRecordingStatus captureAcquisitionRecordingStatus (
    bool callbacksAreActive,
    ProcessorGraph& graph)
{
    return AcquisitionRecordingStatusDetail::
        captureFromGraph (
            callbacksAreActive,
            graph);
}

AcquisitionRecordingStatus captureAcquisitionRecordingStatus (
    AudioComponent& audio,
    ProcessorGraph& graph)
{
    return AcquisitionRecordingStatusDetail::
        captureFromOwners (
            audio,
            graph);
}
