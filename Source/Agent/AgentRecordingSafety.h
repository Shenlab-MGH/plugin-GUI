/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2026 Open Ephys Agent contributors

    ------------------------------------------------------------------
*/

#pragma once

#include "AgentState.h"

#include <cstdint>

struct AgentRecordingGateContext
{
    bool agentInitiated;
    std::uint64_t expectedRevision;
};

struct AgentRecordingGateObservation
{
    bool hasRecordNodes;
    bool recordNodeDirectoriesValid;
    bool synchronized;
    bool directoryPrepared;
    AgentObservedMode mode;
    std::uint64_t revision;
};

enum class AgentRecordingGateOutcome
{
    ready,
    syncBlocked,
    directoryNotPrepared,
    recordNodesNotReady,
    stateUnknown,
    revisionConflict
};

struct AgentRecordingGateDecision
{
    AgentRecordingGateOutcome outcome;
};

class AgentRecordingSafety
{
public:
    AgentRecordingGateDecision evaluate (
        const AgentRecordingGateContext& context,
        const AgentRecordingGateObservation& observation) const;
};
