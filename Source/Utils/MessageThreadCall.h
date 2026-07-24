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

#ifndef MESSAGE_THREAD_CALL_H
#define MESSAGE_THREAD_CALL_H

#include "../../JuceLibraryCode/JuceHeader.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

enum class MessageThreadCallStatus
{
    completed,
    dispatchFailed,
    timedOut,
    failed
};

template <typename Value>
struct MessageThreadCallResult
{
    MessageThreadCallStatus status = MessageThreadCallStatus::dispatchFailed;
    std::optional<Value> value;
    String error;
};

template <typename Operation, typename Dispatcher>
auto runDispatchedCall (Operation&& operation,
                        Dispatcher&& dispatcher,
                        std::chrono::milliseconds timeout)
    -> MessageThreadCallResult<std::invoke_result_t<std::decay_t<Operation>>>
{
    using Callable = std::decay_t<Operation>;
    using Value = std::invoke_result_t<Callable>;
    static_assert (! std::is_void_v<Value>, "Message-thread calls must return a value.");

    struct SharedState
    {
        explicit SharedState (Callable&& callable)
            : operation (std::move (callable))
        {
        }

        Callable operation;
        std::promise<void> completion;
        std::optional<Value> value;
        String error;
        std::atomic<bool> started { false };
        std::atomic<bool> cancelled { false };
    };

    auto state = std::make_shared<SharedState> (Callable (std::forward<Operation> (operation)));
    auto completion = state->completion.get_future();

    std::function<void()> dispatchedOperation = [state]
    {
        state->started.store (true);

        if (! state->cancelled.load())
        {
            try
            {
                state->value.emplace (state->operation());
            }
            catch (const std::exception& exception)
            {
                state->error = String::fromUTF8 (exception.what());
            }
            catch (...)
            {
                state->error = "Unknown message-thread operation failure.";
            }
        }

        state->completion.set_value();
    };

    bool dispatched = false;
    try
    {
        dispatched = dispatcher (std::move (dispatchedOperation));
    }
    catch (const std::exception& exception)
    {
        return { MessageThreadCallStatus::dispatchFailed,
                 std::nullopt,
                 String::fromUTF8 (exception.what()) };
    }
    catch (...)
    {
        return { MessageThreadCallStatus::dispatchFailed,
                 std::nullopt,
                 "Unknown message-thread dispatch failure." };
    }

    if (! dispatched)
        return { MessageThreadCallStatus::dispatchFailed,
                 std::nullopt,
                 "Could not dispatch the operation to the message thread." };

    if (completion.wait_for (timeout) != std::future_status::ready)
    {
        if (! state->started.load())
        {
            state->cancelled.store (true);
            return { MessageThreadCallStatus::timedOut,
                     std::nullopt,
                     "Timed out waiting for the message-thread operation." };
        }

        completion.wait();
    }

    if (state->error.isNotEmpty())
        return { MessageThreadCallStatus::failed, std::nullopt, state->error };

    return { MessageThreadCallStatus::completed,
             std::move (state->value),
             {} };
}

#endif
