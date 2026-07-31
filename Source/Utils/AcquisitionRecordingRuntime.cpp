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

#include "AcquisitionRecordingRuntime.h"

#include "../Audio/AudioComponent.h"
#include "../Processors/ProcessorGraph/ProcessorGraph.h"
#include "../Processors/RecordNode/RecordNode.h"

AcquisitionRecordingControlSnapshot
captureAcquisitionRecordingControlSnapshot (
    AudioComponent& audio,
    ProcessorGraph& graph)
{
    auto* messageManager =
        MessageManager::getInstanceWithoutCreating();
    jassert (
        messageManager != nullptr
        && messageManager->isThisTheMessageThread());

    return AcquisitionRecordingRuntimeDetail::
        captureFromOwners (audio, graph);
}
