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

#include "AgentTransportEndpoint.h"

#include <chrono>
#include <utility>

namespace
{
std::uint64_t monotonicMilliseconds()
{
    return static_cast<std::uint64_t> (
        std::chrono::duration_cast<std::chrono::milliseconds> (
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool isFailsafeTarget (
    AgentObservedMode current,
    AgentObservedMode target)
{
    return target == AgentObservedMode::idle
        || (current == AgentObservedMode::record
            && target == AgentObservedMode::acquire);
}
}

std::shared_ptr<AgentTransportEndpoint>
AgentTransportEndpoint::create (
    Scheduler scheduler,
    AgentStateSnapshot initialState)
{
    return std::shared_ptr<AgentTransportEndpoint> (
        new AgentTransportEndpoint (
            std::move (scheduler),
            std::move (initialState)));
}

AgentTransportEndpoint::AgentTransportEndpoint (
    Scheduler schedulerToUse,
    AgentStateSnapshot initialState)
    : scheduler (std::move (schedulerToUse)),
      stateCache (std::move (initialState))
{
}

void AgentTransportEndpoint::attachExecutor (
    const std::shared_ptr<AgentTransportExecutor>& next)
{
    const std::lock_guard<std::mutex> lock (lifecycleMutex);
    if (! shuttingDown && next)
    {
        executor = next;
        phase = AgentEndpointPhase::ready;
    }
}

void AgentTransportEndpoint::detachExecutor (
    const std::shared_ptr<AgentTransportExecutor>& current)
{
    const std::lock_guard<std::mutex> lock (lifecycleMutex);
    const auto attached = executor.lock();
    if (attached && attached == current)
    {
        executor.reset();
        phase = AgentEndpointPhase::detached;
        mailbox.cancelPending (
            AgentMailboxTerminalReason::executorDetached);
    }
}

AgentMailboxSubmitOutcome AgentTransportEndpoint::submit (
    const AgentTransportRequest& request)
{
    Scheduler schedulerToUse;

    {
        const std::lock_guard<std::mutex> lock (lifecycleMutex);

        if (shuttingDown)
            return AgentMailboxSubmitOutcome::shuttingDown;

        if (executor.expired() || ! scheduler)
            return AgentMailboxSubmitOutcome::unavailable;

        const auto outcome = mailbox.submit (request);
        if (outcome != AgentMailboxSubmitOutcome::accepted)
            return outcome;

        schedulerToUse = scheduler;
    }

    const auto weakEndpoint = weak_from_this();
    bool scheduled = false;

    try
    {
        scheduled = schedulerToUse (
            [weakEndpoint, requestId = request.requestId]
            {
                if (const auto endpoint = weakEndpoint.lock())
                    endpoint->drain (requestId);
            });
    }
    catch (...)
    {
        scheduled = false;
    }

    if (! scheduled)
    {
        mailbox.cancelPending (
            request.requestId,
            AgentMailboxTerminalReason::dispatchUnavailable);
        return AgentMailboxSubmitOutcome::unavailable;
    }

    return AgentMailboxSubmitOutcome::accepted;
}

AgentMailboxLookup AgentTransportEndpoint::query (
    const std::string& requestId) const
{
    return mailbox.lookup (requestId);
}

void AgentTransportEndpoint::publish (AgentStateSnapshot state)
{
    stateCache.publish (std::move (state));
}

AgentStateSnapshot AgentTransportEndpoint::snapshot() const
{
    return stateCache.snapshot();
}

AgentEndpointSnapshot
AgentTransportEndpoint::serviceSnapshot() const
{
    const std::lock_guard<std::mutex> lock (lifecycleMutex);
    const auto effectivePhase =
        phase == AgentEndpointPhase::ready
                && executor.expired()
            ? AgentEndpointPhase::detached
            : phase;
    return { effectivePhase, stateCache.snapshot() };
}

void AgentTransportEndpoint::beginShutdown()
{
    const std::lock_guard<std::mutex> lock (lifecycleMutex);
    shuttingDown = true;
    phase = AgentEndpointPhase::stopped;
    executor.reset();
    mailbox.shutdown();
}

void AgentTransportEndpoint::drain (
    const std::string& requestId)
{
    std::shared_ptr<AgentTransportExecutor> executorToUse;

    {
        const std::lock_guard<std::mutex> lock (lifecycleMutex);

        if (! shuttingDown)
            executorToUse = executor.lock();
    }

    if (! executorToUse)
    {
        mailbox.cancelPending (
            requestId,
            AgentMailboxTerminalReason::executorDetached);
        return;
    }

    const auto request = mailbox.takePending();
    if (! request || request->requestId != requestId)
        return;

    const auto currentState = stateCache.snapshot();
    if (request->deadlineMonotonicMs != 0
        && monotonicMilliseconds() > request->deadlineMonotonicMs
        && ! isFailsafeTarget (
            currentState.mode,
            request->targetMode))
    {
        mailbox.complete (
            request->requestId,
            {
                AgentTransportApplyOutcome::rejected,
                {
                    AgentTransportPlanOutcome::invalidRequest,
                    request->requestId,
                    currentState.mode,
                    request->targetMode,
                    request->expectedRevision,
                    {}
                },
                currentState
            });
        return;
    }

    AgentTransportApplyResult result;

    try
    {
        result = executorToUse->apply (*request);
    }
    catch (...)
    {
        result = {
            AgentTransportApplyOutcome::executionFailed,
            {
                AgentTransportPlanOutcome::invalidRequest,
                request->requestId,
                AgentObservedMode::unknown,
                request->targetMode,
                request->expectedRevision,
                {}
            },
            stateCache.snapshot()
        };
    }

    const bool completed = mailbox.complete (
        request->requestId,
        result);
    (void) completed;
}
