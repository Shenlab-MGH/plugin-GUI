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

#include "ControlStatusJson.h"
#include "json.hpp"

#include <map>
#include <set>

namespace
{
using json = nlohmann::json;

StatusRequestParseResult invalidStatusRequest (const String& error)
{
    return { std::nullopt, "invalid_request", error };
}

bool isSupportedStatusRequestField (const std::string& field)
{
    return field == "mode"
           || field == "confirm_unsynchronized";
}

bool isSupportedRecordingOptionsField (const std::string& field)
{
    return field == "expanded"
           || field == "force_new_directory"
           || field == "new_directory_requested";
}

bool readOptionalBoolean (const json& document,
                          const char* field,
                          std::optional<bool>& destination,
                          String& error)
{
    const auto value = document.find (field);
    if (value == document.end())
        return true;

    if (! value->is_boolean())
    {
        error = "Field '" + String (field) + "' must be a boolean.";
        return false;
    }

    destination = value->get<bool>();
    return true;
}
} // namespace

StatusRequestParseResult parseStatusRequest (StringRef requestBody)
{
    bool duplicateFieldFound = false;
    std::map<int, std::set<std::string>> fieldsByDepth;

    const auto rejectDuplicateFields =
        [&duplicateFieldFound, &fieldsByDepth] (
            int depth,
            json::parse_event_t event,
            json& parsed)
        {
            if (event == json::parse_event_t::object_start)
                fieldsByDepth[depth + 1].clear();
            else if (event == json::parse_event_t::key)
            {
                const auto field = parsed.get<std::string>();
                if (! fieldsByDepth[depth].insert (field).second)
                    duplicateFieldFound = true;
            }

            return true;
        };

    json document;
    try
    {
        document = json::parse (
            String (requestBody).toStdString(),
            rejectDuplicateFields);
    }
    catch (const json::exception& exception)
    {
        return invalidStatusRequest (
            "Invalid JSON request: "
            + String::fromUTF8 (exception.what()));
    }

    if (duplicateFieldFound)
        return invalidStatusRequest (
            "Status request fields must not be duplicated.");

    if (! document.is_object())
        return invalidStatusRequest (
            "Status request must be a JSON object.");

    for (const auto& item : document.items())
    {
        if (! isSupportedStatusRequestField (item.key()))
        {
            return invalidStatusRequest (
                "Unknown status request field: "
                + String (item.key()));
        }
    }

    const auto modeValue = document.find ("mode");
    if (modeValue == document.end())
        return invalidStatusRequest (
            "Field 'mode' is required.");

    if (! modeValue->is_string())
        return invalidStatusRequest (
            "Field 'mode' must be a string.");

    const auto modeName = modeValue->get<std::string>();
    AcquisitionRecordingMode mode;
    if (modeName == "IDLE")
        mode = AcquisitionRecordingMode::idle;
    else if (modeName == "ACQUIRE")
        mode = AcquisitionRecordingMode::acquire;
    else if (modeName == "RECORD")
        mode = AcquisitionRecordingMode::record;
    else
    {
        return invalidStatusRequest (
            "Field 'mode' must be exactly IDLE, ACQUIRE, or RECORD.");
    }

    bool confirmUnsynchronized = false;
    const auto confirmation =
        document.find ("confirm_unsynchronized");
    if (confirmation != document.end())
    {
        if (! confirmation->is_boolean())
        {
            return invalidStatusRequest (
                "Field 'confirm_unsynchronized' must be a boolean.");
        }

        if (mode != AcquisitionRecordingMode::record)
        {
            return invalidStatusRequest (
                "Field 'confirm_unsynchronized' is valid only for RECORD.");
        }

        confirmUnsynchronized = confirmation->get<bool>();
    }

    return {
        StatusRequest { mode, confirmUnsynchronized },
        {},
        {}
    };
}

RecordingOptionsUpdateParseResult parseRecordingOptionsUpdate (StringRef requestBody)
{
    json document;
    try
    {
        document = json::parse (String (requestBody).toStdString());
    }
    catch (const json::exception& exception)
    {
        return { std::nullopt,
                 "Invalid JSON request: " + String::fromUTF8 (exception.what()) };
    }

    if (! document.is_object())
        return { std::nullopt, "Recording options request must be a JSON object." };

    if (document.empty())
        return { std::nullopt, "Recording options request must contain at least one field." };

    for (const auto& item : document.items())
    {
        if (! isSupportedRecordingOptionsField (item.key()))
            return { std::nullopt,
                     "Unknown recording options field: " + String (item.key()) };
    }

    RecordingOptionsUpdate update;
    String error;
    if (! readOptionalBoolean (document, "expanded", update.expanded, error)
        || ! readOptionalBoolean (document, "force_new_directory", update.forceNewDirectory, error)
        || ! readOptionalBoolean (document, "new_directory_requested", update.newDirectoryRequested, error))
        return { std::nullopt, error };

    if (update.forceNewDirectory == true
        && update.newDirectoryRequested == false)
    {
        return { std::nullopt,
                 "new_directory_requested cannot be false when force_new_directory is true." };
    }

    return { std::move (update), {} };
}
