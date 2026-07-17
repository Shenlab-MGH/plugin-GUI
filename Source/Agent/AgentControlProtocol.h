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

#include "AgentTransportEndpoint.h"
#include "AgentExperimentDirectoryEndpoint.h"

#include <optional>
#include <string>

struct AgentControlParseResult
{
    std::optional<AgentTransportRequest> request;
    std::string expectedSessionId;
    std::string error;
};

struct AgentDirectoryParseResult
{
    std::optional<AgentDirectoryEndpointRequest> request;
    std::string expectedSessionId;
    std::string error;
};

namespace AgentControlProtocol
{
AgentControlParseResult parseTransportRequest (
    const std::string& body);

AgentDirectoryParseResult parseDirectoryRequest (
    const std::string& body);

std::string serializeStatus (
    const AgentEndpointSnapshot& snapshot,
    const std::string& sessionId,
    bool mutationEnabled);

std::string serializeRequestLookup (
    const std::string& requestId,
    const AgentMailboxLookup& lookup,
    const std::string& sessionId);

std::string serializeSubmitReceipt (
    const std::string& requestId,
    AgentMailboxSubmitOutcome outcome,
    const std::string& sessionId);

std::string serializeDirectorySnapshot (
    const AgentRecordingDirectorySnapshot& snapshot,
    const std::string& sessionId);

std::string serializeDirectoryRequestLookup (
    const std::string& requestId,
    const AgentDirectoryLookup& lookup,
    const std::string& sessionId);

std::string serializeDirectorySubmitReceipt (
    const std::string& requestId,
    AgentDirectorySubmitOutcome outcome,
    const std::string& sessionId);
}
