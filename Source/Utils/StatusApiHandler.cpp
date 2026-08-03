/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2024 Open Ephys

    ------------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "StatusApiHandler.h"

#include <optional>
#include <string>

namespace
{
using json = nlohmann::json;

const char* modeToString (StatusMode mode)
{
    switch (mode)
    {
        case StatusMode::Idle:
            return "IDLE";
        case StatusMode::Acquire:
            return "ACQUIRE";
        case StatusMode::Record:
            return "RECORD";
    }

    return "IDLE";
}
std::optional<StatusMode> modeFromString (const std::string& mode)
{
    if (mode == "IDLE")
        return StatusMode::Idle;
    if (mode == "ACQUIRE")
        return StatusMode::Acquire;
    if (mode == "RECORD")
        return StatusMode::Record;
    return std::nullopt;
}

void setJsonResponse (httplib::Response& response, int status, const json& body)
{
    response.status = status;
    response.set_content (body.dump(), "application/json");
}

void setErrorResponse (
    httplib::Response& response,
    int status,
    const char* code,
    const char* message)
{
    setJsonResponse (
        response,
        status,
        { { "error", { { "code", code }, { "message", message } } } });
}
} // namespace

void handleStatusGet (
    const httplib::Request&,
    httplib::Response& response,
    const StatusApiHandlers& handlers)
{
    setJsonResponse (
        response,
        200,
        { { "mode", modeToString (handlers.readMode()) } });
}

void handleStatusPut (
    const httplib::Request& request,
    httplib::Response& response,
    const StatusApiHandlers& handlers)
{
    json requestBody;
    try
    {
        requestBody = json::parse (request.body);
    }
    catch (const json::parse_error&)
    {
        setErrorResponse (
            response,
            400,
            "invalid_json",
            "Request body must be valid JSON.");
        return;
    }

    if (! requestBody.is_object()
        || ! requestBody.contains ("mode")
        || ! requestBody["mode"].is_string())
    {
        setErrorResponse (
            response,
            400,
            "invalid_status_request",
            "Request body must be an object with a string 'mode' field.");
        return;
    }

    const auto requestedText = requestBody["mode"].get<std::string>();
    const auto requestedMode = modeFromString (requestedText);
    if (! requestedMode.has_value())
    {
        setErrorResponse (
            response,
            400,
            "unsupported_mode",
            "Mode must be one of IDLE, ACQUIRE, or RECORD.");
        return;
    }

    handlers.requestMode (*requestedMode);
    const auto actualMode = handlers.readMode();

    if (actualMode != *requestedMode)
    {
        setJsonResponse (
            response,
            409,
            { { "error",
                { { "code", "transition_rejected" },
                  { "message", "Open Ephys did not reach the requested mode." } } },
              { "requested_mode", requestedText },
              { "mode", modeToString (actualMode) } });
        return;
    }

    setJsonResponse (
        response,
        200,
        { { "mode", modeToString (actualMode) } });
}
