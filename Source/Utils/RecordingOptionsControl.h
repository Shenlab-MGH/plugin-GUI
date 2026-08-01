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

struct RecordingOptionsApplyResult
{
    std::optional<RecordingOptionsStatus> status;
    String errorCode;
    String errorMessage;
};

template <typename ReadStatus,
          typename SetExpanded,
          typename SetForceNewDirectory,
          typename SetNewDirectoryRequested>
RecordingOptionsApplyResult applyRecordingOptionsUpdate (
    const RecordingOptionsUpdate& update,
    ReadStatus&& readStatus,
    SetExpanded&& setExpanded,
    SetForceNewDirectory&& setForceNewDirectory,
    SetNewDirectoryRequested&& setNewDirectoryRequested)
{
    const auto statusBeforeUpdate = readStatus();
    if (update.newDirectoryRequested.has_value()
        && ! statusBeforeUpdate.newDirectoryRequestAvailable)
    {
        return { std::nullopt,
                 "operation_not_available",
                 "new_directory_requested is disabled in the GUI's current state." };
    }

    if (update.expanded.has_value())
        setExpanded (*update.expanded);
    if (update.forceNewDirectory.has_value())
        setForceNewDirectory (*update.forceNewDirectory);
    if (update.newDirectoryRequested.has_value())
        setNewDirectoryRequested (*update.newDirectoryRequested);

    return { readStatus(), {}, {} };
}

namespace RecordingOptionsControlDetail
{
template <typename Dispatcher, typename ApplyUpdate>
RecordingOptionsControlResult handleRecordingOptionsPutImpl (
    StringRef requestBody,
    Dispatcher&& dispatcher,
    ApplyUpdate&& applyUpdate,
    std::chrono::milliseconds timeout,
    MessageThreadCallGeneration* generation)
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
    auto operation = [update = std::move (update),
                      apply = std::forward<ApplyUpdate> (applyUpdate)]() mutable
    {
        return apply (update);
    };
    auto operationResult = generation != nullptr
        ? runDispatchedCall (
            std::move (operation),
            std::forward<Dispatcher> (dispatcher),
            timeout,
            *generation)
        : runDispatchedCall (
            std::move (operation),
            std::forward<Dispatcher> (dispatcher),
            timeout);

    switch (operationResult.status)
    {
        case MessageThreadCallStatus::completed:
        {
            auto applyResult = std::move (*operationResult.value);
            if (! applyResult.status.has_value())
                return { 409,
                         std::nullopt,
                         applyResult.errorCode,
                         applyResult.errorMessage };

            return { 200, std::move (applyResult.status), {}, {} };
        }

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
} // namespace RecordingOptionsControlDetail

template <typename Dispatcher, typename ApplyUpdate>
RecordingOptionsControlResult handleRecordingOptionsPut (
    StringRef requestBody,
    Dispatcher&& dispatcher,
    ApplyUpdate&& applyUpdate,
    std::chrono::milliseconds timeout)
{
    return RecordingOptionsControlDetail::handleRecordingOptionsPutImpl (
        requestBody,
        std::forward<Dispatcher> (dispatcher),
        std::forward<ApplyUpdate> (applyUpdate),
        timeout,
        nullptr);
}

template <typename Dispatcher, typename ApplyUpdate>
RecordingOptionsControlResult handleRecordingOptionsPut (
    StringRef requestBody,
    Dispatcher&& dispatcher,
    ApplyUpdate&& applyUpdate,
    std::chrono::milliseconds timeout,
    MessageThreadCallGeneration& generation)
{
    return RecordingOptionsControlDetail::handleRecordingOptionsPutImpl (
        requestBody,
        std::forward<Dispatcher> (dispatcher),
        std::forward<ApplyUpdate> (applyUpdate),
        timeout,
        &generation);
}

#endif
