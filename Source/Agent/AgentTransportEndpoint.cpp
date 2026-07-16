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

#include <utility>

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
    if (! shuttingDown)
        executor = next;
}

void AgentTransportEndpoint::detachExecutor (
    const std::shared_ptr<AgentTransportExecutor>& current)
{
    const std::lock_guard<std::mutex> lock (lifecycleMutex);
    const auto attached = executor.lock();
    if (attached && attached == current)
        executor.reset();
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
        mailbox.cancelPending (request.requestId);
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

void AgentTransportEndpoint::beginShutdown()
{
    const std::lock_guard<std::mutex> lock (lifecycleMutex);
    shuttingDown = true;
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
        mailbox.cancelPending (requestId);
        return;
    }

    const auto request = mailbox.takePending();
    if (! request || request->requestId != requestId)
        return;

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
