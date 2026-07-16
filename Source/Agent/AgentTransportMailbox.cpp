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

#include "AgentTransportMailbox.h"

bool AgentTransportMailbox::samePayload (
    const AgentTransportRequest& lhs,
    const AgentTransportRequest& rhs)
{
    return lhs.requestId == rhs.requestId
        && lhs.targetMode == rhs.targetMode
        && lhs.expectedRevision == rhs.expectedRevision;
}

AgentMailboxSubmitOutcome AgentTransportMailbox::submit (
    const AgentTransportRequest& request)
{
    if (request.requestId.empty()
        || request.targetMode == AgentObservedMode::unknown)
    {
        return AgentMailboxSubmitOutcome::invalidRequest;
    }

    const std::lock_guard<std::mutex> lock (mutex);

    if (stopped)
        return AgentMailboxSubmitOutcome::shuttingDown;

    if (const auto found = seen.find (request.requestId);
        found != seen.end())
    {
        return samePayload (found->second, request)
            ? AgentMailboxSubmitOutcome::duplicate
            : AgentMailboxSubmitOutcome::idConflict;
    }

    if (pending || active)
        return AgentMailboxSubmitOutcome::busy;

    seen.emplace (request.requestId, request);
    pending = request;
    return AgentMailboxSubmitOutcome::accepted;
}

std::optional<AgentTransportRequest>
AgentTransportMailbox::takePending()
{
    const std::lock_guard<std::mutex> lock (mutex);

    if (! pending)
        return std::nullopt;

    active = pending;
    pending.reset();
    return active;
}

bool AgentTransportMailbox::complete (
    const std::string& requestId,
    const AgentTransportApplyResult& result)
{
    const std::lock_guard<std::mutex> lock (mutex);

    if (! active || active->requestId != requestId)
        return false;

    completed.emplace (requestId, result);
    completionOrder.push_back (requestId);
    active.reset();

    while (completionOrder.size() > maxCompletedResults)
    {
        completed.erase (completionOrder.front());
        completionOrder.pop_front();
    }

    return true;
}

bool AgentTransportMailbox::cancelPending (
    const std::string& requestId,
    AgentMailboxTerminalReason reason)
{
    if (reason == AgentMailboxTerminalReason::none)
        return false;

    const std::lock_guard<std::mutex> lock (mutex);

    if (! pending || pending->requestId != requestId)
        return false;

    cancelled.emplace (requestId, reason);
    pending.reset();
    return true;
}

bool AgentTransportMailbox::cancelPending (
    AgentMailboxTerminalReason reason)
{
    if (reason == AgentMailboxTerminalReason::none)
        return false;

    const std::lock_guard<std::mutex> lock (mutex);

    if (! pending)
        return false;

    cancelled.emplace (pending->requestId, reason);
    pending.reset();
    return true;
}

AgentMailboxLookup AgentTransportMailbox::lookup (
    const std::string& requestId) const
{
    const std::lock_guard<std::mutex> lock (mutex);

    if (pending && pending->requestId == requestId)
        return {
            AgentMailboxRequestState::pending,
            AgentMailboxTerminalReason::none,
            std::nullopt
        };

    if (active && active->requestId == requestId)
        return {
            AgentMailboxRequestState::active,
            AgentMailboxTerminalReason::none,
            std::nullopt
        };

    if (const auto found = completed.find (requestId);
        found != completed.end())
    {
        return {
            AgentMailboxRequestState::completed,
            AgentMailboxTerminalReason::none,
            found->second
        };
    }

    if (const auto found = cancelled.find (requestId);
        found != cancelled.end())
    {
        return {
            AgentMailboxRequestState::cancelled,
            found->second,
            std::nullopt
        };
    }

    if (seen.find (requestId) != seen.end())
    {
        return {
            AgentMailboxRequestState::expired,
            AgentMailboxTerminalReason::none,
            std::nullopt
        };
    }

    return {};
}

std::optional<AgentTransportApplyResult>
AgentTransportMailbox::resultFor (
    const std::string& requestId) const
{
    return lookup (requestId).result;
}

void AgentTransportMailbox::shutdown()
{
    const std::lock_guard<std::mutex> lock (mutex);
    stopped = true;

    if (pending)
    {
        cancelled.emplace (
            pending->requestId,
            AgentMailboxTerminalReason::shutdown);
        pending.reset();
    }

}
