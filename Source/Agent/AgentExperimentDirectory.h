/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2026 Open Ephys Agent contributors

    ------------------------------------------------------------------
*/

#pragma once

#include "AgentState.h"

#include <cstdint>
#include <string>
#include <vector>

struct AgentDirectoryRequest
{
    std::string approvedRoot;
    std::string directoryName;
    std::uint64_t expectedRevision;
};

struct AgentDirectoryObservation
{
    bool targetExists;
    std::vector<std::string> siblingNames;
    AgentObservedMode mode;
    std::uint64_t revision;
};

enum class AgentDirectoryOutcome
{
    ready,
    approvedRootInvalid,
    invalidNativeName,
    autoSuffixForbidden,
    alreadyExists,
    caseCollision,
    recordingNotInactive,
    revisionConflict
};

struct AgentDirectoryDecision
{
    AgentDirectoryOutcome outcome;
    std::string targetPath;
};

struct AgentRecordingDirectorySnapshot
{
    std::string approvedRoot;
    std::string directoryName;
    std::string targetPath;
    bool prepared = false;
    bool targetExists = false;
    AgentObservedMode mode = AgentObservedMode::unknown;
    std::uint64_t revision = 0;
};

class AgentExperimentDirectory
{
public:
    AgentDirectoryDecision validate (
        const AgentDirectoryRequest& request,
        const AgentDirectoryObservation& observation) const;
};
