/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2026 Open Ephys Agent contributors

    ------------------------------------------------------------------
*/

#include "ControlPanelExperimentAdapter.h"

#include "../UI/ControlPanel.h"

ControlPanelExperimentAdapter::ControlPanelExperimentAdapter (
    ControlPanel& owner)
    : controlPanel (&owner)
{
}

AgentDirectoryApplyResult ControlPanelExperimentAdapter::apply (
    const AgentDirectoryRequest& request)
{
    const std::lock_guard<std::mutex> lock (mutex);
    if (controlPanel == nullptr)
        return { { AgentDirectoryOutcome::approvedRootInvalid, {} }, {} };
    return controlPanel->prepareAgentRecordingDirectory (request);
}

AgentRecordingDirectorySnapshot
ControlPanelExperimentAdapter::snapshot()
{
    const std::lock_guard<std::mutex> lock (mutex);
    if (controlPanel == nullptr)
        return {};
    return controlPanel->getAgentRecordingDirectorySnapshot();
}

void ControlPanelExperimentAdapter::detach (ControlPanel& expected)
{
    const std::lock_guard<std::mutex> lock (mutex);
    if (controlPanel == &expected)
        controlPanel = nullptr;
}
