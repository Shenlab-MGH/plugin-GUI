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

#ifndef ACQUISITION_RECORDING_STATUS_H
#define ACQUISITION_RECORDING_STATUS_H

#include "../TestableExport.h"

#include <cstddef>
#include <optional>
#include <vector>

class AudioComponent;
class ProcessorGraph;

enum class AcquisitionRecordingMode
{
    idle,
    acquire,
    record
};

struct RecordNodeRuntimeState
{
    bool recordingActive = false;
    bool writerThreadRunning = false;
};

/**
    A transport-neutral copy of the achieved acquisition and recording state.

    It deliberately contains no references to GUI components, processors,
    threads, files, parameters, or metadata.
*/
struct AcquisitionRecordingStatus
{
    std::optional<AcquisitionRecordingMode> mode;
    bool acquisitionActive = false;
    bool recordingActive = false;
    std::size_t recordNodeCount = 0;
    std::size_t activeRecordNodeCount = 0;
    std::size_t writerThreadRunningCount = 0;
    bool recordingConsistent = false;
};

/** Derives the canonical achieved state from copied runtime facts. */
TESTABLE AcquisitionRecordingStatus deriveAcquisitionRecordingStatus (
    bool callbacksAreActive,
    const std::vector<RecordNodeRuntimeState>& recordNodes);

/**
    Copies the current Record Node facts from the graph and derives achieved
    state. Must be called on the JUCE message thread.
*/
TESTABLE AcquisitionRecordingStatus captureAcquisitionRecordingStatus (
    bool callbacksAreActive,
    ProcessorGraph& graph);

/**
    Copies callback, Record Node, and writer-thread facts from their official
    runtime owners. Must be called on the JUCE message thread.
*/
TESTABLE AcquisitionRecordingStatus captureAcquisitionRecordingStatus (
    AudioComponent& audio,
    ProcessorGraph& graph);

#endif
