/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2026 Open Ephys Agent contributors

    ------------------------------------------------------------------
*/

#include "AgentExperimentDirectoryEndpoint.h"

#include <utility>

std::shared_ptr<AgentExperimentDirectoryEndpoint>
AgentExperimentDirectoryEndpoint::create (
    Scheduler scheduler,
    AgentRecordingDirectorySnapshot initialSnapshot)
{
    return std::shared_ptr<AgentExperimentDirectoryEndpoint> (
        new AgentExperimentDirectoryEndpoint (
            std::move (scheduler),
            std::move (initialSnapshot)));
}

AgentExperimentDirectoryEndpoint::AgentExperimentDirectoryEndpoint (
    Scheduler schedulerToUse,
    AgentRecordingDirectorySnapshot initialSnapshot)
    : scheduler (std::move (schedulerToUse)),
      currentSnapshot (std::move (initialSnapshot))
{
}

void AgentExperimentDirectoryEndpoint::attachExecutor (
    const std::shared_ptr<AgentExperimentDirectoryExecutor>& next)
{
    const std::lock_guard<std::mutex> lock (mutex);
    if (! stopped && next)
        executor = next;
}

void AgentExperimentDirectoryEndpoint::detachExecutor (
    const std::shared_ptr<AgentExperimentDirectoryExecutor>& current)
{
    const std::lock_guard<std::mutex> lock (mutex);
    const auto attached = executor.lock();
    if (attached && attached == current)
    {
        executor.reset();
        pending.reset();
    }
}

bool AgentExperimentDirectoryEndpoint::samePayload (
    const AgentDirectoryEndpointRequest& lhs,
    const AgentDirectoryEndpointRequest& rhs)
{
    return lhs.runId == rhs.runId
        && lhs.commandId == rhs.commandId
        && lhs.request.approvedRoot == rhs.request.approvedRoot
        && lhs.request.directoryName == rhs.request.directoryName
        && lhs.request.expectedRevision == rhs.request.expectedRevision;
}

AgentDirectorySubmitOutcome AgentExperimentDirectoryEndpoint::submit (
    const AgentDirectoryEndpointRequest& request)
{
    Scheduler schedulerToUse;
    {
        const std::lock_guard<std::mutex> lock (mutex);
        if (stopped)
            return AgentDirectorySubmitOutcome::shuttingDown;
        if (executor.expired() || ! scheduler)
            return AgentDirectorySubmitOutcome::unavailable;
        if (request.runId.empty() || request.commandId.empty()
            || request.request.approvedRoot.empty()
            || request.request.directoryName.empty())
        {
            return AgentDirectorySubmitOutcome::invalidRequest;
        }
        if (const auto found = seen.find (request.commandId);
            found != seen.end())
        {
            return samePayload (found->second, request)
                ? AgentDirectorySubmitOutcome::duplicate
                : AgentDirectorySubmitOutcome::idConflict;
        }
        if (pending || active)
            return AgentDirectorySubmitOutcome::busy;
        seen.emplace (request.commandId, request);
        pending = request;
        schedulerToUse = scheduler;
    }

    const auto weakEndpoint = weak_from_this();
    bool scheduled = false;
    try
    {
        scheduled = schedulerToUse (
            [weakEndpoint, commandId = request.commandId]
            {
                if (const auto endpoint = weakEndpoint.lock())
                    endpoint->drain (commandId);
            });
    }
    catch (...)
    {
        scheduled = false;
    }

    if (! scheduled)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        if (pending && pending->commandId == request.commandId)
            pending.reset();
        return AgentDirectorySubmitOutcome::unavailable;
    }
    return AgentDirectorySubmitOutcome::accepted;
}

AgentDirectoryLookup AgentExperimentDirectoryEndpoint::query (
    const std::string& commandId) const
{
    const std::lock_guard<std::mutex> lock (mutex);
    if (pending && pending->commandId == commandId)
        return { AgentDirectoryRequestState::pending, std::nullopt };
    if (active && active->commandId == commandId)
        return { AgentDirectoryRequestState::active, std::nullopt };
    if (const auto found = completed.find (commandId);
        found != completed.end())
    {
        return { AgentDirectoryRequestState::completed, found->second };
    }
    if (seen.find (commandId) != seen.end())
        return { AgentDirectoryRequestState::expired, std::nullopt };
    return {};
}

AgentRecordingDirectorySnapshot
AgentExperimentDirectoryEndpoint::snapshot() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return currentSnapshot;
}

void AgentExperimentDirectoryEndpoint::publish (
    AgentRecordingDirectorySnapshot snapshot)
{
    const std::lock_guard<std::mutex> lock (mutex);
    currentSnapshot = std::move (snapshot);
}

void AgentExperimentDirectoryEndpoint::publishTransportState (
    AgentObservedMode mode,
    std::uint64_t revision)
{
    const std::lock_guard<std::mutex> lock (mutex);
    currentSnapshot.mode = mode;
    currentSnapshot.revision = revision;
}

void AgentExperimentDirectoryEndpoint::beginShutdown()
{
    const std::lock_guard<std::mutex> lock (mutex);
    stopped = true;
    executor.reset();
    pending.reset();
}

void AgentExperimentDirectoryEndpoint::drain (
    const std::string& commandId)
{
    std::shared_ptr<AgentExperimentDirectoryExecutor> executorToUse;
    AgentDirectoryEndpointRequest request;
    {
        const std::lock_guard<std::mutex> lock (mutex);
        if (stopped || ! pending || pending->commandId != commandId)
            return;
        executorToUse = executor.lock();
        if (! executorToUse)
        {
            pending.reset();
            return;
        }
        request = *pending;
        active = request;
        pending.reset();
    }

    AgentDirectoryApplyResult result;
    try
    {
        result = executorToUse->apply (request.request);
    }
    catch (...)
    {
        result = {
            { AgentDirectoryOutcome::approvedRootInvalid, {} },
            snapshot()
        };
    }

    const std::lock_guard<std::mutex> lock (mutex);
    if (! active || active->commandId != commandId)
        return;
    currentSnapshot = result.snapshot;
    completed.emplace (commandId, result);
    completionOrder.push_back (commandId);
    active.reset();
    while (completionOrder.size() > maxCompletedResults)
    {
        completed.erase (completionOrder.front());
        completionOrder.pop_front();
    }
}
