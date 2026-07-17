/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2026 Open Ephys Agent contributors

    ------------------------------------------------------------------
*/

#include "AgentExperimentDirectory.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <regex>
#include <unordered_set>

namespace
{
std::string foldAscii (std::string value)
{
    std::transform (
        value.begin(),
        value.end(),
        value.begin(),
        [] (unsigned char character)
        { return static_cast<char> (std::tolower (character)); });
    return value;
}

bool hasAutoSuffix (const std::string& value)
{
    static const std::regex suffix { R"( \([1-9][0-9]*\)$)" };
    return std::regex_search (value, suffix);
}

bool isReservedName (const std::string& value)
{
    static const std::unordered_set<std::string> reserved {
        "con", "prn", "aux", "nul",
        "com1", "com2", "com3", "com4", "com5",
        "com6", "com7", "com8", "com9",
        "lpt1", "lpt2", "lpt3", "lpt4", "lpt5",
        "lpt6", "lpt7", "lpt8", "lpt9"
    };
    return reserved.find (foldAscii (value)) != reserved.end();
}

bool isValidNativeName (const std::string& value)
{
    if (value.empty() || value.size() > 120 || isReservedName (value))
        return false;
    if (std::isspace (static_cast<unsigned char> (value.back())) != 0)
        return false;
    for (const unsigned char character : value)
    {
        const bool allowed = std::isalnum (character) != 0
            || character == '_'
            || character == '-'
            || character == ' ';
        if (! allowed)
            return false;
    }
    return true;
}
}

AgentDirectoryDecision AgentExperimentDirectory::validate (
    const AgentDirectoryRequest& request,
    const AgentDirectoryObservation& observation) const
{
    if (observation.revision != request.expectedRevision)
        return { AgentDirectoryOutcome::revisionConflict, {} };

    if (observation.mode != AgentObservedMode::idle)
        return { AgentDirectoryOutcome::recordingNotInactive, {} };

    const std::filesystem::path root { request.approvedRoot };
    if (! root.is_absolute()
        || ! root.has_root_name()
        || root.lexically_normal() == root.root_path())
        return { AgentDirectoryOutcome::approvedRootInvalid, {} };

    if (hasAutoSuffix (request.directoryName))
        return { AgentDirectoryOutcome::autoSuffixForbidden, {} };

    if (! isValidNativeName (request.directoryName))
        return { AgentDirectoryOutcome::invalidNativeName, {} };

    const auto target = (root / request.directoryName).lexically_normal();
    const auto targetText = target.string();

    if (observation.targetExists)
        return { AgentDirectoryOutcome::alreadyExists, targetText };

    const auto candidate = foldAscii (request.directoryName);
    for (const auto& sibling : observation.siblingNames)
    {
        if (foldAscii (sibling) == candidate)
            return { AgentDirectoryOutcome::caseCollision, targetText };
    }

    return { AgentDirectoryOutcome::ready, targetText };
}

