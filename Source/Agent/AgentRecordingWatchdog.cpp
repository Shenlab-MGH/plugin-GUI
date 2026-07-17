/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2026 Open Ephys Agent contributors

    ------------------------------------------------------------------
*/

#include "AgentRecordingWatchdog.h"

AgentRecordingWatchdog::AgentRecordingWatchdog (
    AgentWatchdogPlan planToUse)
    : plan (planToUse)
{
}

AgentWatchdogResult AgentRecordingWatchdog::tick (
    const AgentWatchdogObservation& observation) const
{
    if (observation.mode == AgentObservedMode::unknown)
    {
        return {
            AgentWatchdogDecision::requireManualTakeover,
            AgentRecordingAction::none
        };
    }

    const auto escalationDeadline =
        plan.targetDurationMilliseconds
        + plan.toleranceMilliseconds
        + plan.maximumOverrunMilliseconds;
    if (observation.elapsedMilliseconds > escalationDeadline)
    {
        return {
            AgentWatchdogDecision::requireManualTakeover,
            AgentRecordingAction::none
        };
    }

    if (observation.elapsedMilliseconds
        < plan.targetDurationMilliseconds)
    {
        return observation.mode == AgentObservedMode::record
            ? AgentWatchdogResult {
                  AgentWatchdogDecision::observe,
                  AgentRecordingAction::none
              }
            : AgentWatchdogResult {
                  AgentWatchdogDecision::requireManualTakeover,
                  AgentRecordingAction::none
              };
    }

    if (observation.mode != AgentObservedMode::record)
    {
        return {
            AgentWatchdogDecision::complete,
            AgentRecordingAction::none
        };
    }

    if (! plan.safetyStopAuthorized || ! observation.controlAvailable)
    {
        return {
            AgentWatchdogDecision::requireManualTakeover,
            AgentRecordingAction::none
        };
    }

    return {
        AgentWatchdogDecision::requestVerifiedStop,
        AgentRecordingAction::stop
    };
}

AgentWatchdogResult AgentRecordingWatchdog::pause (
    const AgentWatchdogObservation&) const
{
    return {
        AgentWatchdogDecision::observe,
        AgentRecordingAction::none
    };
}
