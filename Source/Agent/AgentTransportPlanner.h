/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2026 Open Ephys Agent contributors

    ------------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    ------------------------------------------------------------------
*/

#pragma once

#include "AgentState.h"

#include <cstdint>
#include <string>
#include <vector>

struct AgentTransportRequest
{
    std::string requestId;
    AgentObservedMode targetMode;
    std::uint64_t expectedRevision;
};

enum class AgentTransportAction
{
    startAcquisition,
    stopAcquisition,
    requestSafeRecordingStart,
    stopRecording
};

struct AgentTransportStep
{
    AgentTransportAction action;
    AgentObservedMode expectedBefore;
    AgentObservedMode expectedAfter;
    bool requiresRecordingSafetyGate;
};

enum class AgentTransportPlanOutcome
{
    proposalReady,
    alreadySatisfied,
    revisionConflict,
    stateUnknown,
    invalidRequest
};

struct AgentTransportPlan
{
    AgentTransportPlanOutcome outcome;
    std::string requestId;
    AgentObservedMode sourceMode;
    AgentObservedMode targetMode;
    std::uint64_t expectedRevision;
    std::vector<AgentTransportStep> steps;
};

class AgentTransportPlanner
{
public:
    AgentTransportPlan plan (
        const AgentStateSnapshot& current,
        const AgentTransportRequest& request) const;
};
