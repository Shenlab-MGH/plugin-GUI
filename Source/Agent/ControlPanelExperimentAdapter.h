/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2026 Open Ephys Agent contributors

    ------------------------------------------------------------------
*/

#pragma once

#include "AgentExperimentDirectoryEndpoint.h"

#include <mutex>

class ControlPanel;

class ControlPanelExperimentAdapter final
    : public AgentExperimentDirectoryExecutor
{
public:
    explicit ControlPanelExperimentAdapter (ControlPanel& owner);

    AgentDirectoryApplyResult apply (
        const AgentDirectoryRequest& request) override;
    AgentRecordingDirectorySnapshot snapshot() override;
    void detach (ControlPanel& expected);

private:
    std::mutex mutex;
    ControlPanel* controlPanel;
};
