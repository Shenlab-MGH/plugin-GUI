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

namespace
{
using json = nlohmann::json;

bool isSupportedField (const std::string& field)
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
        if (! isSupportedField (item.key()))
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
