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

#include "AgentTransportCoordinator.h"

#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

enum class AgentMailboxSubmitOutcome
{
    accepted,
    duplicate,
    idConflict,
    busy,
    invalidRequest,
    shuttingDown,
    unavailable
};

enum class AgentMailboxRequestState
{
    unknown,
    pending,
    active,
    completed,
    expired,
    cancelled
};

enum class AgentMailboxTerminalReason
{
    none,
    dispatchUnavailable,
    executorDetached,
    shutdown
};

struct AgentMailboxLookup
{
    AgentMailboxRequestState state =
        AgentMailboxRequestState::unknown;
    AgentMailboxTerminalReason terminalReason =
        AgentMailboxTerminalReason::none;
    std::optional<AgentTransportApplyResult> result;
};

class AgentTransportMailbox
{
public:
    AgentMailboxSubmitOutcome submit (
        const AgentTransportRequest& request);

    std::optional<AgentTransportRequest> takePending();

    bool complete (
        const std::string& requestId,
        const AgentTransportApplyResult& result);

    bool cancelPending (
        const std::string& requestId,
        AgentMailboxTerminalReason reason);

    bool cancelPending (AgentMailboxTerminalReason reason);

    AgentMailboxLookup lookup (
        const std::string& requestId) const;

    std::optional<AgentTransportApplyResult> resultFor (
        const std::string& requestId) const;

    void shutdown();

private:
    static bool samePayload (
        const AgentTransportRequest& lhs,
        const AgentTransportRequest& rhs);

    static constexpr std::size_t maxCompletedResults = 128;

    mutable std::mutex mutex;
    std::optional<AgentTransportRequest> pending;
    std::optional<AgentTransportRequest> active;
    std::unordered_map<std::string, AgentTransportRequest> seen;
    std::unordered_map<std::string, AgentTransportApplyResult> completed;
    std::unordered_map<
        std::string,
        AgentMailboxTerminalReason> cancelled;
    std::deque<std::string> completionOrder;
    bool stopped = false;
};
