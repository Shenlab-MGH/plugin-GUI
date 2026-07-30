/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2024 Open Ephys

    ------------------------------------------------------------------
*/

#ifndef RECORD_NODE_RECORDING_START_H
#define RECORD_NODE_RECORDING_START_H

#include "../../../JuceLibraryCode/JuceHeader.h"

#include <optional>

enum class RecordNodeRecordingDirectoryState
{
    missing,
    directory,
    nonDirectory
};

enum class RecordNodeRecordingStartError
{
    writerAlreadyRunning,
    recordingDirectoryCreateFailed,
    writerThreadStartFailed
};

struct RecordNodeRecordingDirectoryCreateResult
{
    bool succeeded = false;
    String detail;
};

struct RecordNodeRecordingStartRequest
{
    int nodeId = 0;
    String recordingDirectory;
    int experimentNumber = 1;
    int recordingIndex = 0;
};

struct RecordNodeRecordingStartResult
{
    int nodeId = 0;
    String recordingDirectory;
    int experimentNumber = 1;
    int recordingIndex = 0;
    /** True once the writer thread launch succeeds. This does not
        guarantee that its asynchronous openFiles call has run. */
    bool writerStarted = false;
    std::optional<RecordNodeRecordingStartError>
        error;
    String detail;
};

template <typename Owner>
RecordNodeRecordingStartResult
startRecordNodeRecording (
    const RecordNodeRecordingStartRequest& request,
    Owner& owner)
{
    RecordNodeRecordingStartResult result {
        request.nodeId,
        request.recordingDirectory,
        request.experimentNumber,
        request.recordingIndex
    };

    if (request.recordingDirectory.isEmpty())
    {
        result.error =
            RecordNodeRecordingStartError::
                recordingDirectoryCreateFailed;
        result.detail =
            "Recording directory is empty.";
        return result;
    }

    if (owner.writerRunning())
    {
        result.error =
            RecordNodeRecordingStartError::
                writerAlreadyRunning;
        result.detail =
            "Record writer thread is already running.";
        return result;
    }

    if (! owner.pipelineAvailable())
    {
        result.error =
            RecordNodeRecordingStartError::
                writerThreadStartFailed;
        result.detail =
            "Record writer pipeline is unavailable.";
        return result;
    }

    if (! owner.settingsPersistenceAvailable())
    {
        result.error =
            RecordNodeRecordingStartError::
                writerThreadStartFailed;
        result.detail =
            "Recording settings persistence"
            " is unavailable.";
        return result;
    }

    const auto directoryState =
        owner.inspectRecordingDirectory (
            request.recordingDirectory);
    if (directoryState
        == RecordNodeRecordingDirectoryState::
            nonDirectory)
    {
        result.error =
            RecordNodeRecordingStartError::
                recordingDirectoryCreateFailed;
        result.detail =
            "Recording path exists but is not a directory.";
        return result;
    }

    if (directoryState
        == RecordNodeRecordingDirectoryState::
            missing)
    {
        const auto created =
            owner.createRecordingDirectory (
                request.recordingDirectory);
        if (! created.succeeded)
        {
            result.error =
                RecordNodeRecordingStartError::
                    recordingDirectoryCreateFailed;
            result.detail =
                created.detail.isNotEmpty()
                    ? created.detail
                    : String (
                        "Could not create"
                        " recording directory.");
            return result;
        }
    }

    owner.prepareRecording();
    owner.setFileComponents (
        request.recordingDirectory,
        request.experimentNumber,
        request.recordingIndex);

    if (! owner.startWriter())
    {
        result.error =
            RecordNodeRecordingStartError::
                writerThreadStartFailed;
        result.detail =
            "Record writer thread failed to start.";
        return result;
    }

    result.writerStarted = true;
    owner.markRecordingActive();

    if (owner.shouldPersistSettings())
    {
        owner.persistSettings (
            request.recordingDirectory,
            request.experimentNumber);
    }

    return result;
}

template <typename Owner>
RecordNodeRecordingStartResult
startRecordNodeRecordingFromOwner (Owner& owner)
{
    const RecordNodeRecordingStartRequest request {
        owner.nodeId(),
        owner.recordingDirectory(),
        owner.experimentNumber(),
        owner.recordingIndex()
    };
    return startRecordNodeRecording (
        request,
        owner);
}

#endif
