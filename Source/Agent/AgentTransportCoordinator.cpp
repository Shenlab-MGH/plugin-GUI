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

#include "AgentTransportCoordinator.h"

#include <chrono>
#include <utility>

namespace
{
AgentTransportApplyResult makeResult (
    AgentTransportApplyOutcome outcome,
    AgentTransportPlan plan,
    AgentStateSnapshot state)
{
    return { outcome, std::move (plan), std::move (state) };
}

std::uint64_t monotonicMilliseconds()
{
    return static_cast<std::uint64_t> (
        std::chrono::duration_cast<std::chrono::milliseconds> (
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool isStartAction (AgentTransportAction action)
{
    return action == AgentTransportAction::startAcquisition
        || action == AgentTransportAction::requestSafeRecordingStart;
}
}

AgentTransportApplyResult AgentTransportCoordinator::apply (
    const AgentTransportRequest& request,
    AgentTransportRuntime& runtime) const
{
    auto state = runtime.readState();
    auto transitionPlan = planner.plan (state, request);

    if (transitionPlan.outcome
        == AgentTransportPlanOutcome::alreadySatisfied)
    {
        return makeResult (
            AgentTransportApplyOutcome::alreadySatisfied,
            std::move (transitionPlan),
            std::move (state));
    }

    if (transitionPlan.outcome
        != AgentTransportPlanOutcome::proposalReady)
    {
        return makeResult (
            AgentTransportApplyOutcome::rejected,
            std::move (transitionPlan),
            std::move (state));
    }

    bool firstStep = true;

    for (const auto& step : transitionPlan.steps)
    {
        const auto before = runtime.readState();
        const bool revisionChangedBeforeCommit =
            firstStep && before.revision != request.expectedRevision;

        if (before.mode != step.expectedBefore
            || revisionChangedBeforeCommit)
        {
            return makeResult (
                AgentTransportApplyOutcome::preconditionChanged,
                std::move (transitionPlan),
                before);
        }

        if (isStartAction (step.action)
            && request.deadlineMonotonicMs != 0
            && monotonicMilliseconds()
                   > request.deadlineMonotonicMs)
        {
            return makeResult (
                AgentTransportApplyOutcome::rejected,
                std::move (transitionPlan),
                before);
        }

        if (! runtime.execute (step.action))
        {
            const auto afterFailure = runtime.readState();
            return makeResult (
                AgentTransportApplyOutcome::executionFailed,
                std::move (transitionPlan),
                afterFailure);
        }

        const auto after = runtime.readState();
        if (after.mode != step.expectedAfter
            || after.revision <= before.revision)
        {
            return makeResult (
                AgentTransportApplyOutcome::readbackMismatch,
                std::move (transitionPlan),
                after);
        }

        state = after;
        firstStep = false;
    }

    return makeResult (
        AgentTransportApplyOutcome::completed,
        std::move (transitionPlan),
        std::move (state));
}
