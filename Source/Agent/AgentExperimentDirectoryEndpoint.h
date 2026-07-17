/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2026 Open Ephys Agent contributors

    ------------------------------------------------------------------
*/

#pragma once

#include "AgentExperimentDirectory.h"

#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

struct AgentDirectoryEndpointRequest
{
    std::string runId;
    std::string commandId;
    AgentDirectoryRequest request;
};

struct AgentDirectoryApplyResult
{
    AgentDirectoryDecision decision;
    AgentRecordingDirectorySnapshot snapshot;
};

class AgentExperimentDirectoryExecutor
{
public:
    virtual ~AgentExperimentDirectoryExecutor() = default;
    virtual AgentDirectoryApplyResult apply (
        const AgentDirectoryRequest& request) = 0;
    virtual AgentRecordingDirectorySnapshot snapshot() = 0;
};

enum class AgentDirectorySubmitOutcome
{
    accepted,
    duplicate,
    idConflict,
    busy,
    invalidRequest,
    unavailable,
    shuttingDown
};

enum class AgentDirectoryRequestState
{
    unknown,
    pending,
    active,
    completed,
    cancelled,
    expired
};

struct AgentDirectoryLookup
{
    AgentDirectoryRequestState state = AgentDirectoryRequestState::unknown;
    std::optional<AgentDirectoryApplyResult> result;
};

class AgentExperimentDirectoryEndpoint final
    : public std::enable_shared_from_this<AgentExperimentDirectoryEndpoint>
{
public:
    using ScheduledCallback = std::function<void()>;
    using Scheduler = std::function<bool (ScheduledCallback)>;

    static std::shared_ptr<AgentExperimentDirectoryEndpoint> create (
        Scheduler scheduler,
        AgentRecordingDirectorySnapshot initialSnapshot);

    void attachExecutor (
        const std::shared_ptr<AgentExperimentDirectoryExecutor>& next);
    void detachExecutor (
        const std::shared_ptr<AgentExperimentDirectoryExecutor>& current);

    AgentDirectorySubmitOutcome submit (
        const AgentDirectoryEndpointRequest& request);
    AgentDirectoryLookup query (const std::string& commandId) const;
    void publish (AgentRecordingDirectorySnapshot snapshot);
    AgentRecordingDirectorySnapshot snapshot() const;
    void beginShutdown();

private:
    AgentExperimentDirectoryEndpoint (
        Scheduler scheduler,
        AgentRecordingDirectorySnapshot initialSnapshot);

    static bool samePayload (
        const AgentDirectoryEndpointRequest& lhs,
        const AgentDirectoryEndpointRequest& rhs);
    void drain (const std::string& commandId);

    static constexpr std::size_t maxCompletedResults = 128;

    mutable std::mutex mutex;
    Scheduler scheduler;
    std::weak_ptr<AgentExperimentDirectoryExecutor> executor;
    std::optional<AgentDirectoryEndpointRequest> pending;
    std::optional<AgentDirectoryEndpointRequest> active;
    std::unordered_map<std::string, AgentDirectoryEndpointRequest> seen;
    std::unordered_map<std::string, AgentDirectoryApplyResult> completed;
    std::deque<std::string> completionOrder;
    AgentRecordingDirectorySnapshot currentSnapshot;
    bool stopped = false;
};
