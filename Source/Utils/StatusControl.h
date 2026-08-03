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

    ------------------------------------------------------------------
*/

#ifndef STATUS_CONTROL_H
#define STATUS_CONTROL_H

#include "MessageThreadCall.h"

#include <chrono>
#include <functional>
#include <utility>

enum class StatusMode
{
    Idle,
    Acquire,
    Record
};

enum class StatusTransitionFailure
{
    none,
    rejected,
    recordNodesNotSynchronized,
    operationUnavailable,
    operationTimedOut,
    operationFailed
};

struct StatusTransitionResult
{
    StatusMode mode = StatusMode::Idle;
    StatusTransitionFailure failure = StatusTransitionFailure::none;
};

struct StatusControlOperations
{
    std::function<StatusMode()> readMode;
    std::function<void (StatusMode)> applyMode;
    std::function<bool()> recordNodesSynchronized;
};

inline StatusTransitionResult applyStatusTransition (
    StatusMode requestedMode,
    const StatusControlOperations& operations)
{
    const auto initialMode = operations.readMode();
    if (initialMode == requestedMode)
        return { initialMode, StatusTransitionFailure::none };

    if (requestedMode == StatusMode::Record
        && ! operations.recordNodesSynchronized())
    {
        return {
            initialMode,
            StatusTransitionFailure::recordNodesNotSynchronized
        };
    }

    operations.applyMode (requestedMode);
    const auto actualMode = operations.readMode();
    return {
        actualMode,
        actualMode == requestedMode ? StatusTransitionFailure::none
                                    : StatusTransitionFailure::rejected
    };
}

template <typename Dispatcher>
StatusTransitionResult requestStatusTransition (
    StatusMode requestedMode,
    StatusControlOperations operations,
    Dispatcher&& dispatcher,
    std::chrono::milliseconds timeout)
{
    const auto call = runDispatchedCall (
        [requestedMode, operations = std::move (operations)]
        { return applyStatusTransition (requestedMode, operations); },
        std::forward<Dispatcher> (dispatcher),
        timeout);

    if (call.status == MessageThreadCallStatus::completed && call.value.has_value())
        return *call.value;
    if (call.status == MessageThreadCallStatus::timedOut)
        return { StatusMode::Idle, StatusTransitionFailure::operationTimedOut };
    if (call.status == MessageThreadCallStatus::failed)
        return { StatusMode::Idle, StatusTransitionFailure::operationFailed };
    return { StatusMode::Idle, StatusTransitionFailure::operationUnavailable };
}

#endif
