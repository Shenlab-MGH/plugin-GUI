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

#include "AgentTransportPlanner.h"

class AgentTransportRuntime
{
public:
    virtual ~AgentTransportRuntime() = default;

    virtual AgentStateSnapshot readState() = 0;
    virtual bool execute (AgentTransportAction action) = 0;
};

enum class AgentTransportApplyOutcome
{
    completed,
    alreadySatisfied,
    rejected,
    wrongThread,
    busy,
    preconditionChanged,
    executionFailed,
    readbackMismatch
};

struct AgentTransportApplyResult
{
    AgentTransportApplyOutcome outcome;
    AgentTransportPlan plan;
    AgentStateSnapshot finalState;
};

class AgentTransportCoordinator
{
public:
    AgentTransportApplyResult apply (
        const AgentTransportRequest& request,
        AgentTransportRuntime& runtime) const;

private:
    AgentTransportPlanner planner;
};
