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

#include "AgentControlProtocol.h"

#if defined (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-literal-operator"
#endif
#include "../Utils/json.hpp"
#if defined (__clang__)
#pragma clang diagnostic pop
#endif

#include <cstdint>
#include <cctype>

namespace
{
using json = nlohmann::json;
constexpr auto schemaVersion =
    "oe-agent-control-preview/v0.0.1";

bool isSafeIdentifier (const std::string& value)
{
    if (value.empty() || value.size() > 128)
        return false;

    for (const auto character : value)
    {
        const auto byte =
            static_cast<unsigned char> (character);
        if (! std::isalnum (byte)
            && character != '.'
            && character != '_'
            && character != ':'
            && character != '-')
        {
            return false;
        }
    }

    return true;
}

const char* modeName (AgentObservedMode mode)
{
    switch (mode)
    {
        case AgentObservedMode::idle:
            return "IDLE";
        case AgentObservedMode::acquire:
            return "ACQUIRE";
        case AgentObservedMode::record:
            return "RECORD";
        case AgentObservedMode::unknown:
            return "UNKNOWN";
    }

    return "UNKNOWN";
}

const char* phaseName (AgentEndpointPhase phase)
{
    switch (phase)
    {
        case AgentEndpointPhase::detached:
            return "DETACHED";
        case AgentEndpointPhase::ready:
            return "READY";
        case AgentEndpointPhase::stopped:
            return "STOPPED";
    }

    return "STOPPED";
}

const char* requestStateName (AgentMailboxRequestState state)
{
    switch (state)
    {
        case AgentMailboxRequestState::unknown:
            return "UNKNOWN";
        case AgentMailboxRequestState::pending:
            return "PENDING";
        case AgentMailboxRequestState::active:
            return "ACTIVE";
        case AgentMailboxRequestState::completed:
            return "COMPLETED";
        case AgentMailboxRequestState::expired:
            return "EXPIRED";
        case AgentMailboxRequestState::cancelled:
            return "CANCELLED";
    }

    return "UNKNOWN";
}

const char* terminalReasonName (
    AgentMailboxTerminalReason reason)
{
    switch (reason)
    {
        case AgentMailboxTerminalReason::none:
            return "NONE";
        case AgentMailboxTerminalReason::dispatchUnavailable:
            return "DISPATCH_UNAVAILABLE";
        case AgentMailboxTerminalReason::executorDetached:
            return "EXECUTOR_DETACHED";
        case AgentMailboxTerminalReason::shutdown:
            return "SHUTDOWN";
    }

    return "NONE";
}

const char* applyOutcomeName (
    AgentTransportApplyOutcome outcome)
{
    switch (outcome)
    {
        case AgentTransportApplyOutcome::completed:
            return "COMPLETED";
        case AgentTransportApplyOutcome::alreadySatisfied:
            return "ALREADY_SATISFIED";
        case AgentTransportApplyOutcome::rejected:
            return "REJECTED";
        case AgentTransportApplyOutcome::wrongThread:
            return "WRONG_THREAD";
        case AgentTransportApplyOutcome::busy:
            return "BUSY";
        case AgentTransportApplyOutcome::preconditionChanged:
            return "PRECONDITION_CHANGED";
        case AgentTransportApplyOutcome::executionFailed:
            return "EXECUTION_FAILED";
        case AgentTransportApplyOutcome::readbackMismatch:
            return "READBACK_MISMATCH";
    }

    return "REJECTED";
}

const char* directoryRequestStateName (
    AgentDirectoryRequestState state)
{
    switch (state)
    {
        case AgentDirectoryRequestState::unknown: return "UNKNOWN";
        case AgentDirectoryRequestState::pending: return "PENDING";
        case AgentDirectoryRequestState::active: return "ACTIVE";
        case AgentDirectoryRequestState::completed: return "COMPLETED";
        case AgentDirectoryRequestState::cancelled: return "CANCELLED";
        case AgentDirectoryRequestState::expired: return "EXPIRED";
    }
    return "UNKNOWN";
}

const char* directoryOutcomeName (AgentDirectoryOutcome outcome)
{
    switch (outcome)
    {
        case AgentDirectoryOutcome::ready: return "READY";
        case AgentDirectoryOutcome::approvedRootInvalid:
            return "APPROVED_ROOT_INVALID";
        case AgentDirectoryOutcome::invalidNativeName:
            return "INVALID_NATIVE_NAME";
        case AgentDirectoryOutcome::autoSuffixForbidden:
            return "AUTO_SUFFIX_FORBIDDEN";
        case AgentDirectoryOutcome::alreadyExists:
        case AgentDirectoryOutcome::caseCollision:
            return "DIRECTORY_COLLISION";
        case AgentDirectoryOutcome::recordingNotInactive:
            return "RECORDING_NOT_INACTIVE";
        case AgentDirectoryOutcome::revisionConflict:
            return "REVISION_CONFLICT";
    }
    return "REJECTED";
}
}

AgentDirectoryParseResult
AgentControlProtocol::parseDirectoryRequest (
    const std::string& body)
{
    try
    {
        const auto value = json::parse (body);
        if (! value.is_object() || value.size() != 6
            || ! value.contains ("run_id")
            || ! value.contains ("command_id")
            || ! value.contains ("expected_session_id")
            || ! value.contains ("approved_root")
            || ! value.contains ("directory_name")
            || ! value.contains ("expected_revision")
            || ! value["run_id"].is_string()
            || ! value["command_id"].is_string()
            || ! value["expected_session_id"].is_string()
            || ! value["approved_root"].is_string()
            || ! value["directory_name"].is_string()
            || ! value["expected_revision"].is_number_integer())
        {
            return { std::nullopt, {}, "INVALID_REQUEST" };
        }

        const auto runId = value["run_id"].get<std::string>();
        const auto commandId = value["command_id"].get<std::string>();
        const auto expectedSessionId =
            value["expected_session_id"].get<std::string>();
        const auto approvedRoot =
            value["approved_root"].get<std::string>();
        const auto directoryName =
            value["directory_name"].get<std::string>();
        const auto revision =
            value["expected_revision"].get<std::int64_t>();
        if (! isSafeIdentifier (runId)
            || ! isSafeIdentifier (commandId)
            || ! isSafeIdentifier (expectedSessionId)
            || approvedRoot.empty() || approvedRoot.size() > 1024
            || directoryName.empty() || directoryName.size() > 255
            || revision < 0)
        {
            return { std::nullopt, {}, "INVALID_REQUEST" };
        }
        return {
            AgentDirectoryEndpointRequest {
                runId,
                commandId,
                {
                    approvedRoot,
                    directoryName,
                    static_cast<std::uint64_t> (revision)
                }
            },
            expectedSessionId,
            {}
        };
    }
    catch (...)
    {
        return { std::nullopt, {}, "INVALID_JSON" };
    }
}

AgentControlParseResult
AgentControlProtocol::parseTransportRequest (
    const std::string& body)
{
    try
    {
        const auto value = json::parse (body);
        if (! value.is_object()
            || ! value.contains ("request_id")
            || ! value.contains ("expected_session_id")
            || ! value.contains ("target_mode")
            || ! value.contains ("expected_revision")
            || ! value["request_id"].is_string()
            || ! value["expected_session_id"].is_string()
            || ! value["target_mode"].is_string()
            || ! value["expected_revision"].is_number_integer())
        {
            return {
                std::nullopt,
                {},
                "INVALID_REQUEST"
            };
        }

        const auto requestId =
            value["request_id"].get<std::string>();
        const auto expectedSessionId =
            value["expected_session_id"].get<std::string>();
        const auto target =
            value["target_mode"].get<std::string>();
        const auto revision =
            value["expected_revision"].get<std::int64_t>();

        if (! isSafeIdentifier (requestId)
            || ! isSafeIdentifier (expectedSessionId)
            || revision < 0)
        {
            return {
                std::nullopt,
                {},
                "INVALID_REQUEST"
            };
        }

        AgentObservedMode mode = AgentObservedMode::unknown;
        if (target == "IDLE")
            mode = AgentObservedMode::idle;
        else if (target == "ACQUIRE")
            mode = AgentObservedMode::acquire;
        else if (target == "RECORD")
            mode = AgentObservedMode::record;
        else
            return {
                std::nullopt,
                {},
                "INVALID_TARGET_MODE"
            };

        return {
            AgentTransportRequest {
                requestId,
                mode,
                static_cast<std::uint64_t> (revision)
            },
            expectedSessionId,
            {}
        };
    }
    catch (...)
    {
        return {
            std::nullopt,
            {},
            "INVALID_JSON"
        };
    }
}

std::string AgentControlProtocol::serializeStatus (
    const AgentEndpointSnapshot& snapshot,
    const std::string& sessionId,
    bool mutationEnabled)
{
    const auto mutationAllowed =
        mutationEnabled
        && snapshot.phase == AgentEndpointPhase::ready
        && snapshot.transport.mode != AgentObservedMode::unknown;

    return json {
        { "schema_version", schemaVersion },
        { "backend", "in-process-agent-endpoint" },
        { "session_id", sessionId },
        { "online",
          snapshot.phase != AgentEndpointPhase::stopped },
        { "mutation_allowed", mutationAllowed },
        { "mutation_disabled_reason",
          mutationAllowed
              ? "NONE"
              : (! mutationEnabled
                     ? "NOT_ARMED"
                     : (snapshot.transport.mode
                                == AgentObservedMode::unknown
                            ? "STATE_UNKNOWN"
                            : "ENDPOINT_NOT_READY")) },
        { "phase", phaseName (snapshot.phase) },
        { "gui_version", snapshot.transport.guiVersion },
        { "mode", modeName (snapshot.transport.mode) },
        { "revision", snapshot.transport.revision }
    }.dump();
}

std::string AgentControlProtocol::serializeRequestLookup (
    const std::string& requestId,
    const AgentMailboxLookup& lookup,
    const std::string& sessionId)
{
    json value {
        { "schema_version", schemaVersion },
        { "session_id", sessionId },
        { "request_id", requestId },
        { "state", requestStateName (lookup.state) },
        { "terminal_reason",
          terminalReasonName (lookup.terminalReason) }
    };

    if (lookup.result)
    {
        value["outcome"] =
            applyOutcomeName (lookup.result->outcome);
        value["final_mode"] =
            modeName (lookup.result->finalState.mode);
        value["final_revision"] =
            lookup.result->finalState.revision;
    }

    return value.dump();
}

std::string AgentControlProtocol::serializeSubmitReceipt (
    const std::string& requestId,
    AgentMailboxSubmitOutcome outcome,
    const std::string& sessionId)
{
    const char* state = "REJECTED";
    const char* reason = "INVALID_REQUEST";

    switch (outcome)
    {
        case AgentMailboxSubmitOutcome::accepted:
            state = "PENDING";
            reason = "NONE";
            break;
        case AgentMailboxSubmitOutcome::duplicate:
            state = "DUPLICATE";
            reason = "NONE";
            break;
        case AgentMailboxSubmitOutcome::idConflict:
            reason = "ID_CONFLICT";
            break;
        case AgentMailboxSubmitOutcome::busy:
            reason = "BUSY";
            break;
        case AgentMailboxSubmitOutcome::invalidRequest:
            reason = "INVALID_REQUEST";
            break;
        case AgentMailboxSubmitOutcome::shuttingDown:
            reason = "SHUTTING_DOWN";
            break;
        case AgentMailboxSubmitOutcome::unavailable:
            reason = "UNAVAILABLE";
            break;
    }

    return json {
        { "schema_version", schemaVersion },
        { "session_id", sessionId },
        { "request_id", requestId },
        { "state", state },
        { "reason", reason }
    }.dump();
}

std::string AgentControlProtocol::serializeDirectorySnapshot (
    const AgentRecordingDirectorySnapshot& snapshot,
    const std::string& sessionId)
{
    return json {
        { "schema_version", schemaVersion },
        { "session_id", sessionId },
        { "approved_root", snapshot.approvedRoot },
        { "directory_name", snapshot.directoryName },
        { "target_path", snapshot.targetPath },
        { "prepared", snapshot.prepared },
        { "target_exists", snapshot.targetExists },
        { "mode", modeName (snapshot.mode) },
        { "revision", snapshot.revision }
    }.dump();
}

std::string AgentControlProtocol::serializeDirectoryRequestLookup (
    const std::string& requestId,
    const AgentDirectoryLookup& lookup,
    const std::string& sessionId)
{
    json value {
        { "schema_version", schemaVersion },
        { "session_id", sessionId },
        { "command_id", requestId },
        { "state", directoryRequestStateName (lookup.state) }
    };
    if (lookup.result)
    {
        value["outcome"] =
            directoryOutcomeName (lookup.result->decision.outcome);
        value["target_path"] = lookup.result->decision.targetPath;
        value["prepared"] = lookup.result->snapshot.prepared;
        value["final_revision"] = lookup.result->snapshot.revision;
    }
    return value.dump();
}

std::string AgentControlProtocol::serializeDirectorySubmitReceipt (
    const std::string& requestId,
    AgentDirectorySubmitOutcome outcome,
    const std::string& sessionId)
{
    const char* state = "REJECTED";
    const char* reason = "INVALID_REQUEST";
    switch (outcome)
    {
        case AgentDirectorySubmitOutcome::accepted:
            state = "PENDING"; reason = "NONE"; break;
        case AgentDirectorySubmitOutcome::duplicate:
            state = "DUPLICATE"; reason = "NONE"; break;
        case AgentDirectorySubmitOutcome::idConflict:
            reason = "ID_CONFLICT"; break;
        case AgentDirectorySubmitOutcome::busy:
            reason = "BUSY"; break;
        case AgentDirectorySubmitOutcome::invalidRequest:
            reason = "INVALID_REQUEST"; break;
        case AgentDirectorySubmitOutcome::unavailable:
            reason = "UNAVAILABLE"; break;
        case AgentDirectorySubmitOutcome::shuttingDown:
            reason = "SHUTTING_DOWN"; break;
    }
    return json {
        { "schema_version", schemaVersion },
        { "session_id", sessionId },
        { "command_id", requestId },
        { "state", state },
        { "reason", reason }
    }.dump();
}
