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

enum class MessageThreadCallStartState
{
    pending,
    started,
    cancelled
};

class MessageThreadCallStartGate
{
public:
    bool tryStart()
    {
        return transitionTo (
            MessageThreadCallStartState::started);
    }

    bool tryCancel()
    {
        return transitionTo (
            MessageThreadCallStartState::cancelled);
    }

    MessageThreadCallStartState state() const
    {
        return current.load (
            std::memory_order_acquire);
    }

private:
    bool transitionTo (
        MessageThreadCallStartState desired)
    {
        auto expected =
            MessageThreadCallStartState::pending;
        return current.compare_exchange_strong (
            expected,
            desired,
            std::memory_order_acq_rel,
            std::memory_order_acquire);
    }

    std::atomic<MessageThreadCallStartState>
        current {
            MessageThreadCallStartState::pending
        };
};

template <typename Value>
struct MessageThreadCallResult
{
    MessageThreadCallStatus status = MessageThreadCallStatus::dispatchFailed;
    std::optional<Value> value;
    String error;
};

namespace MessageThreadCallDetail
{
template <typename Operation, typename Dispatcher>
auto runDispatchedCallWithGate (
    Operation&& operation,
    Dispatcher&& dispatcher,
    std::chrono::milliseconds timeout,
    std::shared_ptr<MessageThreadCallStartGate> gate)
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
    };

    auto state = std::make_shared<SharedState> (Callable (std::forward<Operation> (operation)));
    auto completion = state->completion.get_future();
    const auto finishStartedOperation =
        [&]() -> MessageThreadCallResult<Value>
    {
        completion.wait();

        if (state->error.isNotEmpty())
        {
            return {
                MessageThreadCallStatus::failed,
                std::nullopt,
                state->error
            };
        }

        return {
            MessageThreadCallStatus::completed,
            std::move (state->value),
            {}
        };
    };

    std::function<void()> dispatchedOperation =
        [state, gate]
    {
        if (gate->tryStart())
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
        if (gate->tryCancel())
        {
            return {
                MessageThreadCallStatus::dispatchFailed,
                std::nullopt,
                String::fromUTF8 (exception.what())
            };
        }

        return finishStartedOperation();
    }
    catch (...)
    {
        if (gate->tryCancel())
        {
            return {
                MessageThreadCallStatus::dispatchFailed,
                std::nullopt,
                "Unknown message-thread dispatch failure."
            };
        }

        return finishStartedOperation();
    }

    if (! dispatched)
    {
        if (gate->tryCancel())
        {
            return {
                MessageThreadCallStatus::dispatchFailed,
                std::nullopt,
                "Could not dispatch the operation to the message thread."
            };
        }

        return finishStartedOperation();
    }

    if (completion.wait_for (timeout) != std::future_status::ready)
    {
        if (gate->tryCancel())
        {
            return { MessageThreadCallStatus::timedOut,
                     std::nullopt,
                     "Timed out waiting for the message-thread operation." };
        }

        return finishStartedOperation();
    }

    return finishStartedOperation();
}
} // namespace MessageThreadCallDetail

template <typename Operation, typename Dispatcher>
auto runDispatchedCall (Operation&& operation,
                        Dispatcher&& dispatcher,
                        std::chrono::milliseconds timeout)
    -> MessageThreadCallResult<std::invoke_result_t<std::decay_t<Operation>>>
{
    return MessageThreadCallDetail::runDispatchedCallWithGate (
        std::forward<Operation> (operation),
        std::forward<Dispatcher> (dispatcher),
        timeout,
        std::make_shared<MessageThreadCallStartGate>());
}

#if defined (BUILD_TESTS)
template <typename Operation, typename Dispatcher>
auto runDispatchedCall (
    Operation&& operation,
    Dispatcher&& dispatcher,
    std::chrono::milliseconds timeout,
    MessageThreadCallStartGate& gate)
    -> MessageThreadCallResult<std::invoke_result_t<std::decay_t<Operation>>>
{
    auto injectedGate =
        std::shared_ptr<MessageThreadCallStartGate> (
            &gate,
            [] (MessageThreadCallStartGate*) {});
    return MessageThreadCallDetail::runDispatchedCallWithGate (
        std::forward<Operation> (operation),
        std::forward<Dispatcher> (dispatcher),
        timeout,
        std::move (injectedGate));
}
#endif

#endif
