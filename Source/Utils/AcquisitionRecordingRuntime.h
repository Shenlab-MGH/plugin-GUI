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

#ifndef ACQUISITION_RECORDING_RUNTIME_H
#define ACQUISITION_RECORDING_RUNTIME_H

#include "AcquisitionRecordingControl.h"
#include "../TestableExport.h"

#include <cstddef>
#include <vector>

class AudioComponent;
class ProcessorGraph;

/**
    Shared owner-read boundary for the concrete runtime overload.

    The template makes the complete accessor loop testable without starting
    audio callbacks or RecordThreads. Its result contains values only and
    retains none of the owners or RecordNode pointers.
*/
namespace AcquisitionRecordingRuntimeDetail
{
template <typename AudioOwner, typename GraphOwner>
AcquisitionRecordingControlSnapshot captureFromOwners (
    AudioOwner& audio,
    GraphOwner& graph)
{
    AcquisitionRecordingControlSnapshot snapshot;
    const bool callbacksAreActive =
        audio.callbacksAreActive();
    snapshot.audioDeviceAvailable =
        audio.checkForDevice();
    snapshot.audioSampleRate =
        static_cast<double> (audio.getSampleRate());
    snapshot.processorGraphReady =
        graph.inspectAcquisitionReadiness().ready;

    const auto currentRecordNodes =
        graph.getRecordNodes();
    snapshot.recordNodes.reserve (
        static_cast<std::size_t> (
            currentRecordNodes.size()));
    std::vector<RecordNodeRuntimeState>
        runtimeStates;
    runtimeStates.reserve (
        static_cast<std::size_t> (
            currentRecordNodes.size()));

    for (auto* node : currentRecordNodes)
    {
        AcquisitionRecordingControlNodeState
            copiedNode;
        copiedNode.generation =
            node->getRuntimeGeneration();
        copiedNode.recordingActive =
            node->getRecordingStatus();
        copiedNode.writerThreadRunning =
            node->isWriterThreadRunning();
        copiedNode.recordingPathValid =
            node->isRecordingPathValid();
        copiedNode.synchronized =
            node->isSynchronized();

        runtimeStates.push_back ({
            copiedNode.recordingActive,
            copiedNode.writerThreadRunning
        });
        snapshot.recordNodes.push_back (
            copiedNode);
    }

    snapshot.status =
        deriveAcquisitionRecordingStatus (
            callbacksAreActive,
            runtimeStates);
    return snapshot;
}
} // namespace AcquisitionRecordingRuntimeDetail

/**
    Copies the status controller's complete runtime readback from the official
    owners. Must be called on the JUCE message thread.
*/
TESTABLE AcquisitionRecordingControlSnapshot
captureAcquisitionRecordingControlSnapshot (
    AudioComponent& audio,
    ProcessorGraph& graph);

#endif
