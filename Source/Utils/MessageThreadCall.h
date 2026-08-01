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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

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

class MessageThreadCallGeneration
{
private:
    struct State;

public:
    class Registration
    {
    public:
        Registration() = default;
        Registration (const Registration&) = delete;
        Registration& operator= (const Registration&) = delete;

        Registration (Registration&& other) noexcept
            : state (std::move (other.state)), identifier (other.identifier)
        {
            other.identifier = 0;
        }

        Registration& operator= (Registration&& other) noexcept
        {
            if (this != &other)
            {
                reset();
                state = std::move (other.state);
                identifier = other.identifier;
                other.identifier = 0;
            }

            return *this;
        }

        ~Registration()
        {
            reset();
        }

        explicit operator bool() const noexcept
        {
            return identifier != 0;
        }

    private:
        Registration (std::shared_ptr<State> registrationState,
                      std::size_t registrationIdentifier)
            : state (std::move (registrationState)), identifier (registrationIdentifier)
        {
        }

        void reset()
        {
            if (identifier == 0)
                return;

            if (auto lockedState = state.lock())
            {
                std::lock_guard<std::mutex> guard (lockedState->mutex);
                auto& cancellations = lockedState->cancellations;
                cancellations.erase (
                    std::remove_if (
                        cancellations.begin(),
                        cancellations.end(),
                        [this] (const auto& cancellation)
                        {
                            return cancellation.first == identifier;
                        }),
                    cancellations.end());
            }

            identifier = 0;
        }

        std::weak_ptr<State> state;
        std::size_t identifier = 0;

        friend class MessageThreadCallGeneration;
    };

    Registration registerCancellation (std::function<void()> cancellation)
    {
        std::lock_guard<std::mutex> guard (state->mutex);

        if (state->quiesced)
            return {};

        const auto identifier = state->nextIdentifier++;
        state->cancellations.emplace_back (identifier, std::move (cancellation));
        return { state, identifier };
    }

    void quiesce()
    {
        std::vector<std::pair<std::size_t, std::function<void()>>> pendingCancellations;

        {
            std::lock_guard<std::mutex> guard (state->mutex);

            if (state->quiesced)
                return;

            state->quiesced = true;
            pendingCancellations.swap (state->cancellations);
        }

        for (const auto& cancellation : pendingCancellations)
        {
            try
            {
                cancellation.second();
            }
            catch (...)
            {
            }
        }
    }

#if defined (BUILD_TESTS)
    std::size_t pendingCancellationCount() const
    {
        std::lock_guard<std::mutex> guard (state->mutex);
        return state->cancellations.size();
    }
#endif

private:
    struct State
    {
        mutable std::mutex mutex;
        bool quiesced = false;
        std::size_t nextIdentifier = 1;
        std::vector<std::pair<std::size_t, std::function<void()>>> cancellations;
    };

    std::shared_ptr<State> state = std::make_shared<State>();
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
#if defined (BUILD_TESTS)
inline std::atomic<int> injectedPreparationFailures { 0 };

inline void failNextCompletionPreparation()
{
    injectedPreparationFailures.fetch_add (1, std::memory_order_release);
}

inline bool consumeInjectedPreparationFailure()
{
    auto remaining = injectedPreparationFailures.load (std::memory_order_acquire);
    while (remaining > 0)
        if (injectedPreparationFailures.compare_exchange_weak (
                remaining,
                remaining - 1,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
            return true;

    return false;
}

inline int injectedPreparationFailureCount()
{
    return injectedPreparationFailures.load (std::memory_order_acquire);
}

inline void clearInjectedPreparationFailures()
{
    injectedPreparationFailures.store (0, std::memory_order_release);
}
#endif

template <typename Operation, typename Dispatcher>
auto runDispatchedCallWithGate (
    Operation&& operation,
    Dispatcher&& dispatcher,
    std::chrono::milliseconds timeout,
    std::shared_ptr<MessageThreadCallStartGate> gate,
    MessageThreadCallGeneration* generation)
    -> MessageThreadCallResult<std::invoke_result_t<std::decay_t<Operation>>>
{
    using Callable = std::decay_t<Operation>;
    using Value = std::invoke_result_t<Callable>;
    static_assert (! std::is_void_v<Value>, "Message-thread calls must return a value.");

    struct SharedState
    {
        explicit SharedState (Callable&& callable)
            : operation (std::move (callable)),
              fallbackFailure (std::make_shared<CompletionResult> (
                  MessageThreadCallStatus::failed,
                  std::nullopt,
                  "Could not prepare the message-thread call result.")),
              dispatchFallback (std::make_shared<CompletionResult> (
                  MessageThreadCallStatus::dispatchFailed,
                  std::nullopt,
                  "Could not prepare the message-thread dispatch result.")),
              timeoutFallback (std::make_shared<CompletionResult> (
                  MessageThreadCallStatus::timedOut,
                  std::nullopt,
                  "Timed out waiting for the message-thread operation.")),
              shutdownCancellation (std::make_shared<CompletionResult> (
                  MessageThreadCallStatus::dispatchFailed,
                  std::nullopt,
                  "Message-thread call cancelled by listener shutdown."))
        {
        }

        Callable operation;
        struct CompletionResult
        {
            CompletionResult (MessageThreadCallStatus completedStatus,
                              std::nullopt_t,
                              String completedError)
                : status (completedStatus),
                  error (std::move (completedError))
            {
            }

            CompletionResult (MessageThreadCallStatus completedStatus,
                              std::in_place_t,
                              Value&& completedValue,
                              String completedError)
                : status (completedStatus),
                  value (std::in_place,
                         std::move (completedValue)),
                  error (std::move (completedError))
            {
            }

            MessageThreadCallStatus status;
            std::optional<Value> value;
            String error;
        };

        std::shared_ptr<CompletionResult> result;
        const std::shared_ptr<CompletionResult> fallbackFailure;
        const std::shared_ptr<CompletionResult> dispatchFallback;
        const std::shared_ptr<CompletionResult> timeoutFallback;
        const std::shared_ptr<CompletionResult> shutdownCancellation;
        std::mutex completionMutex;
        std::condition_variable completionChanged;
        bool completed = false;

        std::shared_ptr<CompletionResult> prepare (
            MessageThreadCallStatus completedStatus,
            String completedError)
        {
#if defined (BUILD_TESTS)
            if (consumeInjectedPreparationFailure())
                throw std::bad_alloc();
#endif
            return std::make_shared<CompletionResult> (
                completedStatus,
                std::nullopt,
                std::move (completedError));
        }

        std::shared_ptr<CompletionResult> prepareValue (
            MessageThreadCallStatus completedStatus,
            Value&& completedValue,
            String completedError)
        {
#if defined (BUILD_TESTS)
            if (consumeInjectedPreparationFailure())
                throw std::bad_alloc();
#endif
            return std::make_shared<CompletionResult> (
                completedStatus,
                std::in_place,
                std::move (completedValue),
                std::move (completedError));
        }

        void publish (std::shared_ptr<CompletionResult> completedResult)
        {
            {
                std::lock_guard<std::mutex> guard (completionMutex);

                if (completed)
                    return;

                result = std::move (completedResult);
                completed = true;
            }

            completionChanged.notify_all();
        }

        bool cancelAndPublish (
            const std::shared_ptr<MessageThreadCallStartGate>& gate,
            std::shared_ptr<CompletionResult> completedResult)
        {
            std::unique_lock<std::mutex> lock (completionMutex);

            if (! gate->tryCancel())
                return false;

            result = std::move (completedResult);
            completed = true;
            lock.unlock();
            completionChanged.notify_all();
            return true;
        }

        std::shared_ptr<CompletionResult> fallbackFor (
            MessageThreadCallStatus completedStatus) const
        {
            return completedStatus == MessageThreadCallStatus::timedOut
                ? timeoutFallback
                : dispatchFallback;
        }
    };

    auto state = std::make_shared<SharedState> (Callable (std::forward<Operation> (operation)));
    const auto finishStartedOperation =
        [&]() -> MessageThreadCallResult<Value>
    {
        std::shared_ptr<typename SharedState::CompletionResult> completedResult;
        {
            std::unique_lock<std::mutex> lock (state->completionMutex);
            state->completionChanged.wait (
                lock,
                [state] { return state->completed; });
            completedResult = state->result;
        }

        try
        {
            return {
                completedResult->status,
                std::move (completedResult->value),
                completedResult->error
            };
        }
        catch (const std::exception& exception)
        {
            try
            {
                return {
                    MessageThreadCallStatus::failed,
                    std::nullopt,
                    String::fromUTF8 (exception.what())
                };
            }
            catch (...)
            {
                return {
                    MessageThreadCallStatus::failed,
                    std::nullopt,
                    "Unknown message-thread result move failure."
                };
            }
        }
        catch (...)
        {
            return {
                MessageThreadCallStatus::failed,
                std::nullopt,
                "Unknown message-thread result move failure."
            };
        }
    };

    const auto cancelPendingCall =
        [state, gate] (MessageThreadCallStatus status, const char* error)
    {
        std::shared_ptr<typename SharedState::CompletionResult> prepared;
        try
        {
            prepared = state->prepare (
                status,
                String::fromUTF8 (error));
        }
        catch (...)
        {
            prepared = state->fallbackFor (status);
        }

        return state->cancelAndPublish (gate, std::move (prepared));
    };

    std::optional<MessageThreadCallGeneration::Registration> generationRegistration;

    if (generation != nullptr)
        generationRegistration.emplace (generation->registerCancellation (
            [weakState = std::weak_ptr<SharedState> (state), gate]
            {
                if (auto lockedState = weakState.lock())
                {
                    lockedState->cancelAndPublish (
                        gate,
                        lockedState->shutdownCancellation);
                }
            }));

    if (generation != nullptr && ! *generationRegistration)
    {
        state->cancelAndPublish (
            gate,
            state->shutdownCancellation);
        return finishStartedOperation();
    }

    std::function<void()> dispatchedOperation =
        [state, gate]
    {
        if (gate->tryStart())
        {
            try
            {
                auto prepared = state->prepareValue (
                    MessageThreadCallStatus::completed,
                    state->operation(),
                    {});
                state->publish (std::move (prepared));
            }
            catch (const std::exception& exception)
            {
                try
                {
                    const auto error = String::fromUTF8 (exception.what());
                    auto prepared = state->prepare (
                        error.isNotEmpty()
                            ? MessageThreadCallStatus::failed
                            : MessageThreadCallStatus::completed,
                        error);
                    state->publish (std::move (prepared));
                }
                catch (...)
                {
                    state->publish (state->fallbackFailure);
                }
            }
            catch (...)
            {
                try
                {
                    auto prepared = state->prepare (
                        MessageThreadCallStatus::failed,
                        "Unknown message-thread operation failure.");
                    state->publish (std::move (prepared));
                }
                catch (...)
                {
                    state->publish (state->fallbackFailure);
                }
            }
        }
    };

    bool dispatched = false;
    try
    {
        dispatched = dispatcher (std::move (dispatchedOperation));
    }
    catch (const std::exception& exception)
    {
        if (cancelPendingCall (
                MessageThreadCallStatus::dispatchFailed,
                exception.what()))
        {
            return finishStartedOperation();
        }

        return finishStartedOperation();
    }
    catch (...)
    {
        if (cancelPendingCall (
                MessageThreadCallStatus::dispatchFailed,
                "Unknown message-thread dispatch failure."))
        {
            return finishStartedOperation();
        }

        return finishStartedOperation();
    }

    if (! dispatched)
    {
        if (cancelPendingCall (
                MessageThreadCallStatus::dispatchFailed,
                "Could not dispatch the operation to the message thread."))
        {
            return finishStartedOperation();
        }

        return finishStartedOperation();
    }

    bool completedBeforeTimeout = false;
    {
        std::unique_lock<std::mutex> lock (state->completionMutex);
        completedBeforeTimeout = state->completionChanged.wait_for (
            lock,
            timeout,
            [state] { return state->completed; });
    }

    if (! completedBeforeTimeout)
    {
        if (cancelPendingCall (
                MessageThreadCallStatus::timedOut,
                "Timed out waiting for the message-thread operation."))
        {
            return finishStartedOperation();
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
        std::make_shared<MessageThreadCallStartGate>(),
        nullptr);
}

template <typename Operation, typename Dispatcher>
auto runDispatchedCall (Operation&& operation,
                        Dispatcher&& dispatcher,
                        std::chrono::milliseconds timeout,
                        MessageThreadCallGeneration& generation)
    -> MessageThreadCallResult<std::invoke_result_t<std::decay_t<Operation>>>
{
    return MessageThreadCallDetail::runDispatchedCallWithGate (
        std::forward<Operation> (operation),
        std::forward<Dispatcher> (dispatcher),
        timeout,
        std::make_shared<MessageThreadCallStartGate>(),
        &generation);
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
        std::move (injectedGate),
        nullptr);
}
#endif

#endif
