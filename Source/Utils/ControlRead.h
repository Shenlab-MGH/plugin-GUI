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

#ifndef CONTROL_READ_H
#define CONTROL_READ_H

#include "MessageThreadCall.h"

#include <chrono>
#include <optional>
#include <type_traits>
#include <utility>

template <typename Value>
struct ControlReadResult
{
    int httpStatus = 500;
    std::optional<Value> value;
    String errorCode;
    String errorMessage;
};

template <typename Dispatcher, typename ReadState>
auto handleControlRead (Dispatcher&& dispatcher,
                        ReadState&& readState,
                        std::chrono::milliseconds timeout)
    -> ControlReadResult<std::invoke_result_t<std::decay_t<ReadState>>>
{
    auto operationResult = runDispatchedCall (
        std::forward<ReadState> (readState),
        std::forward<Dispatcher> (dispatcher),
        timeout);

    using Value = std::invoke_result_t<std::decay_t<ReadState>>;
    switch (operationResult.status)
    {
        case MessageThreadCallStatus::completed:
            return ControlReadResult<Value> { 200,
                                              std::move (operationResult.value),
                                              {},
                                              {} };

        case MessageThreadCallStatus::dispatchFailed:
            return ControlReadResult<Value> { 503,
                                              std::nullopt,
                                              "operation_unavailable",
                                              operationResult.error };

        case MessageThreadCallStatus::timedOut:
            return ControlReadResult<Value> { 504,
                                              std::nullopt,
                                              "operation_timeout",
                                              operationResult.error };

        case MessageThreadCallStatus::failed:
            return ControlReadResult<Value> { 500,
                                              std::nullopt,
                                              "operation_failed",
                                              operationResult.error };
    }

    jassertfalse;
    return ControlReadResult<Value> { 500,
                                      std::nullopt,
                                      "operation_failed",
                                      "Unknown control read result." };
}

#endif
