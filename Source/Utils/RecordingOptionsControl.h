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

#ifndef RECORDING_OPTIONS_CONTROL_H
#define RECORDING_OPTIONS_CONTROL_H

#include "ControlStatus.h"
#include "ControlStatusJson.h"
#include "MessageThreadCall.h"

#include <chrono>
#include <optional>
#include <utility>

struct RecordingOptionsControlResult
{
    int httpStatus = 500;
    std::optional<RecordingOptionsStatus> status;
    String errorCode;
    String errorMessage;
};

template <typename Dispatcher, typename ApplyUpdate>
RecordingOptionsControlResult handleRecordingOptionsPut (
    StringRef requestBody,
    Dispatcher&& dispatcher,
    ApplyUpdate&& applyUpdate,
    std::chrono::milliseconds timeout)
{
    auto parsed = parseRecordingOptionsUpdate (requestBody);
    if (! parsed.update.has_value())
    {
        return { 400,
                 std::nullopt,
                 "invalid_request",
                 parsed.error };
    }

    auto update = std::move (*parsed.update);
    auto operationResult = runDispatchedCall (
        [update = std::move (update),
         apply = std::forward<ApplyUpdate> (applyUpdate)]() mutable
        {
            return apply (update);
        },
        std::forward<Dispatcher> (dispatcher),
        timeout);

    switch (operationResult.status)
    {
        case MessageThreadCallStatus::completed:
            return { 200,
                     std::move (operationResult.value),
                     {},
                     {} };

        case MessageThreadCallStatus::dispatchFailed:
            return { 503,
                     std::nullopt,
                     "operation_unavailable",
                     operationResult.error };

        case MessageThreadCallStatus::timedOut:
            return { 504,
                     std::nullopt,
                     "operation_timeout",
                     operationResult.error };

        case MessageThreadCallStatus::failed:
            return { 500,
                     std::nullopt,
                     "operation_failed",
                     operationResult.error };
    }

    jassertfalse;
    return { 500,
             std::nullopt,
             "operation_failed",
             "Unknown recording options operation result." };
}

#endif
