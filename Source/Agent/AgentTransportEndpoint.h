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

#include "AgentStateSnapshotCache.h"
#include "AgentTransportMailbox.h"

#include <functional>
#include <memory>
#include <mutex>

class AgentTransportExecutor
{
public:
    virtual ~AgentTransportExecutor() = default;

    virtual AgentTransportApplyResult apply (
        const AgentTransportRequest& request) = 0;
};

enum class AgentEndpointPhase
{
    detached,
    ready,
    stopped
};

struct AgentEndpointSnapshot
{
    AgentEndpointPhase phase = AgentEndpointPhase::detached;
    AgentStateSnapshot transport;
};

class AgentTransportEndpoint final
    : public std::enable_shared_from_this<AgentTransportEndpoint>
{
public:
    using ScheduledCallback = std::function<void()>;
    using Scheduler = std::function<bool (ScheduledCallback)>;

    static std::shared_ptr<AgentTransportEndpoint> create (
        Scheduler scheduler,
        AgentStateSnapshot initialState);

    void attachExecutor (
        const std::shared_ptr<AgentTransportExecutor>& next);

    void detachExecutor (
        const std::shared_ptr<AgentTransportExecutor>& current);

    AgentMailboxSubmitOutcome submit (
        const AgentTransportRequest& request);

    AgentMailboxLookup query (
        const std::string& requestId) const;

    void publish (AgentStateSnapshot state);
    AgentStateSnapshot snapshot() const;
    AgentEndpointSnapshot serviceSnapshot() const;

    void beginShutdown();

private:
    AgentTransportEndpoint (
        Scheduler scheduler,
        AgentStateSnapshot initialState);

    void drain (const std::string& requestId);

    mutable std::mutex lifecycleMutex;
    Scheduler scheduler;
    std::weak_ptr<AgentTransportExecutor> executor;
    bool shuttingDown = false;
    AgentEndpointPhase phase = AgentEndpointPhase::detached;
    AgentTransportMailbox mailbox;
    AgentStateSnapshotCache stateCache;
};
