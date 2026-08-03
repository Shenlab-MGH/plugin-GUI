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

#include "ControlCapabilityJson.h"

namespace
{
const char* kindToString (ControlCapabilityKind kind)
{
    switch (kind)
    {
        case ControlCapabilityKind::action:
            return "action";
        case ControlCapabilityKind::toggle:
            return "toggle";
        case ControlCapabilityKind::value:
            return "value";
        case ControlCapabilityKind::range:
            return "range";
        case ControlCapabilityKind::status:
            return "status";
        case ControlCapabilityKind::selection:
            return "selection";
        case ControlCapabilityKind::collection:
            return "collection";
    }

    jassertfalse;
    return "status";
}

nlohmann::json stringArrayToJson (const StringArray& values)
{
    auto result = nlohmann::json::array();

    for (const auto& value : values)
        result.push_back (value.toStdString());

    return result;
}
} // namespace

nlohmann::json controlCapabilitiesToJson (const std::vector<ControlCapability>& capabilities)
{
    nlohmann::json result;
    result["contract_version"] = "0.1.3";
    result["capabilities"] = nlohmann::json::array();

    for (const auto& capability : capabilities)
    {
        nlohmann::json item;
        item["id"] = capability.id.toStdString();
        item["name"] = capability.name.toStdString();
        item["description"] = capability.description.toStdString();
        item["kind"] = kindToString (capability.kind);
        item["uia"]["automation_id"] = capability.uiaAutomationId.toStdString();
        item["api"] = nlohmann::json::array();

        for (const auto& operation : capability.operations)
        {
            item["api"].push_back ({ { "operation", operation.operation.toStdString() },
                                     { "method", operation.method.toStdString() },
                                     { "path", operation.path.toStdString() },
                                     { "request_fields", stringArrayToJson (operation.requestFields) },
                                     { "response_fields", stringArrayToJson (operation.responseFields) } });
        }

        if (capability.modeSemantics.has_value())
        {
            const auto& semantics = *capability.modeSemantics;
            item["mode_semantics"] = {
                { "field", semantics.field.toStdString() },
                { "allowed_values", stringArrayToJson (semantics.allowedValues) },
                { "on_values", stringArrayToJson (semantics.onValues) },
                { "off_values", stringArrayToJson (semantics.offValues) },
                { "commands",
                  { { "on", { { semantics.field.toStdString(), semantics.onCommandValue.toStdString() } } },
                    { "off", { { semantics.field.toStdString(), semantics.offCommandValue.toStdString() } } } } },
                { "response_meaning", semantics.responseMeaning.toStdString() }
            };
        }

        result["capabilities"].push_back (std::move (item));
    }

    return result;
}
