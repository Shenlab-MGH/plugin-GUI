/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2024 Open Ephys

    ------------------------------------------------------------------
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
    const auto result = handleControlRead (
        std::forward<Dispatcher> (dispatcher),
        std::forward<ReadInventory> (readInventory),
        timeout);

    if (! result.value.has_value())
    {
        nlohmann::json document;
        document["ok"] = false;
        document["capability"] = "oe.control.signal_chain.processors";
        document["error"]["code"] = result.errorCode.toStdString();
        document["error"]["message"] = result.errorMessage.toStdString();
        response.status = result.httpStatus;
        response.set_content (document.dump(), "application/json");
        return;
    }

    response.status = 200;
    response.set_content (result.value->dump(), "application/json");
}

#endif
