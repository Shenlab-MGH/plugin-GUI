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

#include "AgentTransportPlanner.h"

#include <utility>

namespace
{
AgentTransportPlan result (
    AgentTransportPlanOutcome outcome,
    const AgentStateSnapshot& current,
    const AgentTransportRequest& request,
    std::vector<AgentTransportStep> steps = {})
{
    return {
        outcome,
        request.requestId,
        current.mode,
        request.targetMode,
        request.expectedRevision,
        std::move (steps)
    };
}
}

AgentTransportPlan AgentTransportPlanner::plan (
    const AgentStateSnapshot& current,
    const AgentTransportRequest& request) const
{
    if (request.requestId.empty()
        || request.targetMode == AgentObservedMode::unknown)
    {
        return result (
            AgentTransportPlanOutcome::invalidRequest,
            current,
            request);
    }

    if (current.mode == AgentObservedMode::unknown)
    {
        return result (
            AgentTransportPlanOutcome::stateUnknown,
            current,
            request);
    }

    if (request.expectedMode != AgentObservedMode::unknown
        && current.mode != request.expectedMode)
    {
        return result (
            AgentTransportPlanOutcome::modeConflict,
            current,
            request);
    }

    if (current.mode == request.targetMode)
    {
        return result (
            AgentTransportPlanOutcome::alreadySatisfied,
            current,
            request);
    }

    if (current.revision != request.expectedRevision)
    {
        return result (
            AgentTransportPlanOutcome::revisionConflict,
            current,
            request);
    }

    switch (current.mode)
    {
        case AgentObservedMode::idle:
            if (request.targetMode == AgentObservedMode::acquire)
            {
                return result (
                    AgentTransportPlanOutcome::proposalReady,
                    current,
                    request,
                    {
                        {
                            AgentTransportAction::startAcquisition,
                            AgentObservedMode::idle,
                            AgentObservedMode::acquire,
                            false
                        }
                    });
            }

            if (request.targetMode == AgentObservedMode::record)
            {
                return result (
                    AgentTransportPlanOutcome::proposalReady,
                    current,
                    request,
                    {
                        {
                            AgentTransportAction::requestSafeRecordingStart,
                            AgentObservedMode::idle,
                            AgentObservedMode::record,
                            true
                        }
                    });
            }
            break;

        case AgentObservedMode::acquire:
            if (request.targetMode == AgentObservedMode::idle)
            {
                return result (
                    AgentTransportPlanOutcome::proposalReady,
                    current,
                    request,
                    {
                        {
                            AgentTransportAction::stopAcquisition,
                            AgentObservedMode::acquire,
                            AgentObservedMode::idle,
                            false
                        }
                    });
            }

            if (request.targetMode == AgentObservedMode::record)
            {
                return result (
                    AgentTransportPlanOutcome::proposalReady,
                    current,
                    request,
                    {
                        {
                            AgentTransportAction::requestSafeRecordingStart,
                            AgentObservedMode::acquire,
                            AgentObservedMode::record,
                            true
                        }
                    });
            }
            break;

        case AgentObservedMode::record:
            if (request.targetMode == AgentObservedMode::acquire)
            {
                return result (
                    AgentTransportPlanOutcome::proposalReady,
                    current,
                    request,
                    {
                        {
                            AgentTransportAction::stopRecording,
                            AgentObservedMode::record,
                            AgentObservedMode::acquire,
                            false
                        }
                    });
            }

            if (request.targetMode == AgentObservedMode::idle)
            {
                return result (
                    AgentTransportPlanOutcome::proposalReady,
                    current,
                    request,
                    {
                        {
                            AgentTransportAction::stopRecording,
                            AgentObservedMode::record,
                            AgentObservedMode::acquire,
                            false
                        },
                        {
                            AgentTransportAction::stopAcquisition,
                            AgentObservedMode::acquire,
                            AgentObservedMode::idle,
                            false
                        }
                    });
            }
            break;

        case AgentObservedMode::unknown:
            break;
    }

    return result (
        AgentTransportPlanOutcome::invalidRequest,
        current,
        request);
}
