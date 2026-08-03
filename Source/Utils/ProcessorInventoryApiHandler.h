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

#ifndef PROCESSOR_INVENTORY_API_HANDLER_H
#define PROCESSOR_INVENTORY_API_HANDLER_H

#include "ControlRead.h"
#include "httplib.h"
#include "json.hpp"

#include <chrono>
#include <set>
#include <utility>
#include <vector>

template <typename Processors, typename SerializeProcessor>
nlohmann::json buildProcessorInventoryDocument (const Processors& processors,
                                                SerializeProcessor&& serializeProcessor)
{
    std::set<int> seenNodeIds;
    std::vector<nlohmann::json> processorsJson;

    for (auto* processor : processors)
    {
        if (processor == nullptr || processor->isEmpty())
            continue;

        if (! seenNodeIds.insert (processor->getNodeId()).second)
            continue;

        processorsJson.push_back (serializeProcessor (processor));
    }

    nlohmann::json document;
    document["processors"] = std::move (processorsJson);
    return document;
}

template <typename Dispatcher, typename ReadInventory>
void handleProcessorInventoryGet (const httplib::Request&,
                                  httplib::Response& response,
                                  Dispatcher&& dispatcher,
                                  ReadInventory&& readInventory,
                                  std::chrono::milliseconds timeout)
{
    const auto readResult = handleControlRead (
        std::forward<Dispatcher> (dispatcher),
        std::forward<ReadInventory> (readInventory),
        timeout);

    if (! readResult.value.has_value())
    {
        nlohmann::json document;
        document["ok"] = false;
        document["capability"] = "oe.control.signal_chain.processors";
        document["error"]["code"] = readResult.errorCode.toStdString();
        document["error"]["message"] = readResult.errorMessage.toStdString();
        response.status = readResult.httpStatus;
        response.set_content (document.dump(), "application/json");
        return;
    }

    response.status = 200;
    response.set_content (readResult.value->dump(), "application/json");
}

#endif
