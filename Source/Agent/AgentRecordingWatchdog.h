/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2026 Open Ephys Agent contributors

    ------------------------------------------------------------------
*/

#pragma once

#include "AgentState.h"

#include <cstdint>

struct AgentWatchdogPlan
{
    std::int64_t targetDurationMilliseconds;
    std::int64_t toleranceMilliseconds;
    std::int64_t maximumOverrunMilliseconds;
    std::int64_t pollCadenceMilliseconds;
    bool safetyStopAuthorized;
};

struct AgentWatchdogObservation
{
    std::int64_t elapsedMilliseconds;
    AgentObservedMode mode;
    bool controlAvailable;
};

enum class AgentWatchdogDecision
{
    observe,
    requestVerifiedStop,
    requireManualTakeover,
    complete
};

enum class AgentRecordingAction
{
    none,
    stop
};

struct AgentWatchdogResult
{
    AgentWatchdogDecision decision;
    AgentRecordingAction recordingAction;
};

class AgentRecordingWatchdog
{
public:
    explicit AgentRecordingWatchdog (AgentWatchdogPlan plan);

    AgentWatchdogResult tick (
        const AgentWatchdogObservation& observation) const;
    AgentWatchdogResult pause (
        const AgentWatchdogObservation& observation) const;

private:
    AgentWatchdogPlan plan;
};
