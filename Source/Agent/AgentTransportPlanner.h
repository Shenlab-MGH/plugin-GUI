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
#include <utility>
#include <vector>

struct AgentTransportRequest
{
    AgentTransportRequest (
        std::string requestIdToUse,
        AgentObservedMode targetModeToUse,
        std::uint64_t expectedRevisionToUse,
        AgentObservedMode expectedModeToUse = AgentObservedMode::unknown,
        std::string runIdToUse = {},
        std::string idempotencyKeyToUse = {},
        std::string approvalIdToUse = {},
        std::string actionParametersHashToUse = {})
        : requestId (std::move (requestIdToUse)),
          targetMode (targetModeToUse),
          expectedRevision (expectedRevisionToUse),
          expectedMode (expectedModeToUse),
          runId (std::move (runIdToUse)),
          idempotencyKey (std::move (idempotencyKeyToUse)),
          approvalId (std::move (approvalIdToUse)),
          actionParametersHash (std::move (actionParametersHashToUse))
    {
    }

    std::string requestId;
    AgentObservedMode targetMode;
    std::uint64_t expectedRevision;
    AgentObservedMode expectedMode = AgentObservedMode::unknown;
    std::string runId;
    std::string idempotencyKey;
    std::string approvalId;
    std::string actionParametersHash;
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
    modeConflict,
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
