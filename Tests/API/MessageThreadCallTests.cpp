#include "../../Source/Utils/MessageThreadCall.h"
#include "gtest/gtest.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;

TEST (MessageThreadCallStartGateTests,
      CancellationAndStartAreMutuallyExclusiveWhenCancellationWins)
{
    MessageThreadCallStartGate gate;

    EXPECT_TRUE (gate.tryCancel());
    EXPECT_FALSE (gate.tryStart());
    EXPECT_EQ (
        gate.state(),
        MessageThreadCallStartState::cancelled);
}

TEST (MessageThreadCallStartGateTests,
      CancellationAndStartAreMutuallyExclusiveWhenStartWins)
{
    MessageThreadCallStartGate gate;

    EXPECT_TRUE (gate.tryStart());
    EXPECT_FALSE (gate.tryCancel());
    EXPECT_EQ (
        gate.state(),
        MessageThreadCallStartState::started);
}

TEST (MessageThreadCallStartGateTests,
      ConcurrentStartAndCancellationHaveExactlyOneWinner)
{
    MessageThreadCallStartGate gate;
    std::promise<void> startReady;
    std::promise<void> cancelReady;
    auto startIsReady =
        startReady.get_future();
    auto cancelIsReady =
        cancelReady.get_future();
    std::promise<void> releaseBoth;
    auto release =
        releaseBoth.get_future().share();
    bool startWon = false;
    bool cancelWon = false;

    std::thread starter (
        [&]
        {
            startReady.set_value();
            release.wait();
            startWon = gate.tryStart();
        });
    std::thread canceller (
        [&]
        {
            cancelReady.set_value();
            release.wait();
            cancelWon = gate.tryCancel();
        });

    startIsReady.wait();
    cancelIsReady.wait();
    releaseBoth.set_value();
    starter.join();
    canceller.join();

    EXPECT_NE (startWon, cancelWon);
    EXPECT_EQ (
        gate.state(),
        startWon
            ? MessageThreadCallStartState::started
            : MessageThreadCallStartState::cancelled);
}

TEST (MessageThreadCallTests, ReturnsTheOperationValueAfterDispatchCompletes)
{
    const auto result = runDispatchedCall (
        [] { return 42; },
        [] (std::function<void()> operation)
        {
            operation();
            return true;
        },
        50ms);

    EXPECT_EQ (result.status, MessageThreadCallStatus::completed);
    ASSERT_TRUE (result.value.has_value());
    EXPECT_EQ (*result.value, 42);
    EXPECT_TRUE (result.error.isEmpty());
}

TEST (MessageThreadCallTests, ReportsDispatchFailureWithoutRunningTheOperation)
{
    bool operationRan = false;
    const auto result = runDispatchedCall (
        [&]
        {
            operationRan = true;
            return 42;
        },
        [] (std::function<void()>) { return false; },
        50ms);

    EXPECT_EQ (result.status, MessageThreadCallStatus::dispatchFailed);
    EXPECT_FALSE (result.value.has_value());
    EXPECT_FALSE (operationRan);
}

TEST (MessageThreadCallTests,
      FalseBeforeStartCancelsAnInjectedGateAndSavedCallback)
{
    int operationCount = 0;
    std::function<void()> queued;
    MessageThreadCallStartGate gate;
    const auto result = runDispatchedCall (
        [&]
        {
            ++operationCount;
            return 42;
        },
        [&] (std::function<void()> operation)
        {
            queued = std::move (operation);
            return false;
        },
        50ms,
        gate);

    EXPECT_EQ (
        result.status,
        MessageThreadCallStatus::dispatchFailed);
    EXPECT_EQ (
        gate.state(),
        MessageThreadCallStartState::cancelled);
    EXPECT_EQ (operationCount, 0);
    ASSERT_TRUE (queued);
    queued();
    EXPECT_EQ (operationCount, 0);
}

TEST (MessageThreadCallTests,
      ThrowBeforeStartCancelsAnInjectedGateAndSavedCallback)
{
    int operationCount = 0;
    std::function<void()> queued;
    MessageThreadCallStartGate gate;
    const auto result = runDispatchedCall (
        [&]
        {
            ++operationCount;
            return 42;
        },
        [&] (std::function<void()> operation) -> bool
        {
            queued = std::move (operation);
            throw std::runtime_error (
                "dispatcher threw before start");
        },
        50ms,
        gate);

    EXPECT_EQ (
        result.status,
        MessageThreadCallStatus::dispatchFailed);
    EXPECT_TRUE (
        result.error.contains (
            "dispatcher threw before start"));
    EXPECT_EQ (
        gate.state(),
        MessageThreadCallStartState::cancelled);
    EXPECT_EQ (operationCount, 0);
    ASSERT_TRUE (queued);
    queued();
    EXPECT_EQ (operationCount, 0);
}

TEST (MessageThreadCallTests,
      StartedOperationResultWinsWhenDispatcherReturnsFalse)
{
    int operationCount = 0;
    MessageThreadCallStartGate gate;
    std::promise<void> operationStarted;
    auto started =
        operationStarted.get_future().share();
    std::promise<void> dispatcherDecided;
    auto decided =
        dispatcherDecided.get_future().share();
    std::promise<void> releaseOperation;
    auto release =
        releaseOperation.get_future().share();
    std::thread messageThread;
    std::thread releaser (
        [&]
        {
            decided.wait();
            releaseOperation.set_value();
        });

    const auto result = runDispatchedCall (
        [&]
        {
            ++operationCount;
            operationStarted.set_value();
            release.wait();
            return 42;
        },
        [&] (std::function<void()> operation)
        {
            messageThread = std::thread (
                [operation = std::move (operation)]() mutable
                {
                    operation();
                });
            started.wait();
            dispatcherDecided.set_value();
            return false;
        },
        50ms,
        gate);

    messageThread.join();
    releaser.join();
    EXPECT_EQ (
        result.status,
        MessageThreadCallStatus::completed);
    ASSERT_TRUE (result.value.has_value());
    EXPECT_EQ (*result.value, 42);
    EXPECT_EQ (operationCount, 1);
    EXPECT_EQ (
        gate.state(),
        MessageThreadCallStartState::started);
}

TEST (MessageThreadCallTests,
      StartedOperationResultWinsWhenDispatcherThrows)
{
    int operationCount = 0;
    MessageThreadCallStartGate gate;
    std::promise<void> operationStarted;
    auto started =
        operationStarted.get_future().share();
    std::promise<void> dispatcherDecided;
    auto decided =
        dispatcherDecided.get_future().share();
    std::promise<void> releaseOperation;
    auto release =
        releaseOperation.get_future().share();
    std::thread messageThread;
    std::thread releaser (
        [&]
        {
            decided.wait();
            releaseOperation.set_value();
        });

    const auto result = runDispatchedCall (
        [&]
        {
            ++operationCount;
            operationStarted.set_value();
            release.wait();
            return 42;
        },
        [&] (std::function<void()> operation) -> bool
        {
            messageThread = std::thread (
                [operation = std::move (operation)]() mutable
                {
                    operation();
                });
            started.wait();
            dispatcherDecided.set_value();
            throw std::runtime_error (
                "dispatcher threw after start");
        },
        50ms,
        gate);

    messageThread.join();
    releaser.join();
    EXPECT_EQ (
        result.status,
        MessageThreadCallStatus::completed);
    ASSERT_TRUE (result.value.has_value());
    EXPECT_EQ (*result.value, 42);
    EXPECT_EQ (operationCount, 1);
    EXPECT_EQ (
        gate.state(),
        MessageThreadCallStartState::started);
}

TEST (MessageThreadCallTests, TimesOutWithoutLeavingDanglingStackReferences)
{
    bool operationRan = false;
    std::function<void()> pendingOperation;
    const auto result = runDispatchedCall (
        [&]
        {
            operationRan = true;
            return 42;
        },
        [&] (std::function<void()> operation)
        {
            pendingOperation = std::move (operation);
            return true;
        },
        1ms);

    EXPECT_EQ (result.status, MessageThreadCallStatus::timedOut);
    EXPECT_FALSE (result.value.has_value());
    ASSERT_TRUE (pendingOperation);

    pendingOperation();
    EXPECT_FALSE (operationRan);
}

TEST (MessageThreadCallTests,
      ZeroDeadlineCancellationWinsAndSavedCallbackCanNeverRunWork)
{
    int operationCount = 0;
    std::function<void()> queued;
    MessageThreadCallStartGate gate;
    const auto result = runDispatchedCall (
        [&]
        {
            ++operationCount;
            return 42;
        },
        [&] (std::function<void()> operation)
        {
            queued = std::move (operation);
            return true;
        },
        0ms,
        gate);

    EXPECT_EQ (
        result.status,
        MessageThreadCallStatus::timedOut);
    EXPECT_EQ (operationCount, 0);
    EXPECT_EQ (
        gate.state(),
        MessageThreadCallStartState::cancelled);
    ASSERT_TRUE (queued);
    queued();
    EXPECT_EQ (operationCount, 0);
    EXPECT_EQ (
        gate.state(),
        MessageThreadCallStartState::cancelled);
}

TEST (MessageThreadCallTests,
      StartClaimBeforeDispatcherReturnsWinsAtZeroDeadline)
{
    int operationCount = 0;
    MessageThreadCallStartGate gate;
    std::promise<void> operationStarted;
    auto started =
        operationStarted.get_future().share();
    std::promise<void> releaseOperation;
    auto release =
        releaseOperation.get_future().share();
    std::thread messageThread;
    std::thread releaser (
        [&]
        {
            started.wait();
            releaseOperation.set_value();
        });

    const auto result = runDispatchedCall (
        [&]
        {
            ++operationCount;
            operationStarted.set_value();
            release.wait();
            return 42;
        },
        [&] (std::function<void()> operation)
        {
            messageThread = std::thread (
                [operation = std::move (operation)]() mutable
                {
                    operation();
                });
            started.wait();
            return true;
        },
        0ms,
        gate);

    messageThread.join();
    releaser.join();
    EXPECT_EQ (
        result.status,
        MessageThreadCallStatus::completed);
    EXPECT_EQ (operationCount, 1);
    EXPECT_EQ (
        gate.state(),
        MessageThreadCallStartState::started);
    ASSERT_TRUE (result.value.has_value());
    EXPECT_EQ (*result.value, 42);
}

TEST (MessageThreadCallTests, WaitsForAnOperationThatAlreadyStartedInsteadOfReportingALateTimeout)
{
    std::promise<void> operationStarted;
    auto started =
        operationStarted.get_future().share();
    std::promise<void> releaseOperation;
    auto release =
        releaseOperation.get_future().share();
    std::thread messageThread;
    std::thread releaser (
        [&]
        {
            started.wait();
            releaseOperation.set_value();
        });

    const auto result = runDispatchedCall (
        [&]
        {
            operationStarted.set_value();
            release.wait();
            return 42;
        },
        [&] (std::function<void()> operation)
        {
            messageThread = std::thread (
                [operation = std::move (operation)]() mutable
                {
                    operation();
                });
            started.wait();
            return true;
        },
        0ms);

    messageThread.join();
    releaser.join();
    EXPECT_EQ (result.status, MessageThreadCallStatus::completed);
    ASSERT_TRUE (result.value.has_value());
    EXPECT_EQ (*result.value, 42);
}

TEST (MessageThreadCallTests, ConvertsOperationExceptionsIntoFailureResults)
{
    const auto result = runDispatchedCall (
        []() -> int { throw std::runtime_error ("operation failed"); },
        [] (std::function<void()> operation)
        {
            operation();
            return true;
        },
        50ms);

    EXPECT_EQ (result.status, MessageThreadCallStatus::failed);
    EXPECT_FALSE (result.value.has_value());
    EXPECT_TRUE (result.error.contains ("operation failed"));
}

TEST (MessageThreadCallTests,
      EmptyStdExceptionMessageKeepsTheLegacyCompletedWithoutValueResult)
{
    class EmptyMessageException : public std::exception
    {
    public:
        const char* what() const noexcept override
        {
            return "";
        }
    };

    const auto result = runDispatchedCall (
        []() -> int { throw EmptyMessageException(); },
        [] (std::function<void()> operation)
        {
            operation();
            return true;
        },
        50ms);

    EXPECT_EQ (result.status, MessageThreadCallStatus::completed);
    EXPECT_FALSE (result.value.has_value());
    EXPECT_TRUE (result.error.isEmpty());
}

TEST (MessageThreadCallTests,
      ThrowingSecondResultMoveCompletesWithFailureInsteadOfLeavingCallerBlocked)
{
    class ThrowsOnSecondMove
    {
    public:
        ThrowsOnSecondMove() = default;
        ThrowsOnSecondMove (const ThrowsOnSecondMove&) = delete;
        ThrowsOnSecondMove& operator= (const ThrowsOnSecondMove&) = delete;

        ThrowsOnSecondMove (ThrowsOnSecondMove&&)
        {
            if (++moveCount() == 2)
                throw std::runtime_error ("second move failed");
        }

        static std::atomic<int>& moveCount()
        {
            static std::atomic<int> count { 0 };
            return count;
        }
    };

    struct Probe
    {
        std::promise<void> finished;
        std::optional<MessageThreadCallResult<ThrowsOnSecondMove>> result;
    };

    ThrowsOnSecondMove::moveCount() = 0;
    auto probe = std::make_shared<Probe>();
    auto finished = probe->finished.get_future();
    std::thread caller (
        [probe]
        {
            probe->result.emplace (runDispatchedCall (
                [] { return ThrowsOnSecondMove(); },
                [] (std::function<void()> callback)
                {
                    callback();
                    return true;
                },
                50ms));
            probe->finished.set_value();
        });

    const auto completedInTime =
        finished.wait_for (1s) == std::future_status::ready;

    if (completedInTime)
        caller.join();
    else
        caller.detach();

    ASSERT_TRUE (completedInTime);
    ASSERT_TRUE (probe->result.has_value());
    EXPECT_EQ (probe->result->status, MessageThreadCallStatus::failed);
    EXPECT_TRUE (probe->result->error.contains ("second move failed"));
}

TEST (MessageThreadCallTests,
      DispatcherExceptionPreparationFailureUsesPrebuiltDispatchFallback)
{
    MessageThreadCallDetail::clearInjectedPreparationFailures();
    MessageThreadCallDetail::failNextCompletionPreparation();

    const auto result = runDispatchedCall (
        [] { return 42; },
        [] (std::function<void()>) -> bool
        {
            throw std::runtime_error ("dispatcher failed");
        },
        50ms);

    const auto remainingPreparationFailures =
        MessageThreadCallDetail::injectedPreparationFailureCount();
    MessageThreadCallDetail::clearInjectedPreparationFailures();

    EXPECT_EQ (remainingPreparationFailures, 0);
    EXPECT_EQ (result.status, MessageThreadCallStatus::dispatchFailed);
    EXPECT_TRUE (result.error.contains ("prepare the message-thread dispatch"));
}

TEST (MessageThreadCallTests,
      SuccessfulResultHasNoExtraMoveBeforeLeavingStagedCompletion)
{
    class ThrowsOnThirdMove
    {
    public:
        ThrowsOnThirdMove() = default;
        ThrowsOnThirdMove (const ThrowsOnThirdMove&) = delete;
        ThrowsOnThirdMove& operator= (const ThrowsOnThirdMove&) = delete;

        ThrowsOnThirdMove (ThrowsOnThirdMove&&)
        {
            if (++moveCount() == 3)
                throw std::runtime_error ("third move failed");
        }

        static std::atomic<int>& moveCount()
        {
            static std::atomic<int> count { 0 };
            return count;
        }
    };

    ThrowsOnThirdMove::moveCount() = 0;
    const auto result = runDispatchedCall (
        [] { return ThrowsOnThirdMove(); },
        [] (std::function<void()> callback)
        {
            callback();
            return true;
        },
        50ms);

    EXPECT_EQ (result.status, MessageThreadCallStatus::completed);
    EXPECT_TRUE (result.value.has_value());
    EXPECT_EQ (ThrowsOnThirdMove::moveCount(), 2);
}

TEST (MessageThreadCallGenerationTests,
      QuiesceCancelsPendingCallWakesWaiterAndMakesLateCallbackANoOp)
{
    struct Probe
    {
        MessageThreadCallGeneration generation;
        std::function<void()> savedCallback;
        std::promise<void> queued;
        std::promise<void> finished;
        std::atomic<int> operationCount { 0 };
        MessageThreadCallResult<int> result;
    };

    auto probe = std::make_shared<Probe>();
    auto queuedFuture = probe->queued.get_future();
    auto finishedFuture = probe->finished.get_future();

    std::thread waiter (
        [probe]
        {
            probe->result = runDispatchedCall (
                [probe]
                {
                    ++probe->operationCount;
                    return 42;
                },
                [probe] (std::function<void()> callback)
                {
                    probe->savedCallback = std::move (callback);
                    probe->queued.set_value();
                    return true;
                },
                5s,
                probe->generation);
            probe->finished.set_value();
        });

    const auto queuedInTime =
        queuedFuture.wait_for (1s) == std::future_status::ready;
    probe->generation.quiesce();
    const auto completedInTime =
        finishedFuture.wait_for (1s) == std::future_status::ready;

    if (completedInTime)
        waiter.join();
    else
        waiter.detach();

    EXPECT_TRUE (queuedInTime);
    ASSERT_TRUE (completedInTime);
    EXPECT_EQ (probe->result.status, MessageThreadCallStatus::dispatchFailed);
    EXPECT_EQ (
        probe->result.error,
        "Message-thread call cancelled by listener shutdown.");
    EXPECT_FALSE (probe->result.value.has_value());
    EXPECT_EQ (probe->operationCount, 0);
    ASSERT_TRUE (probe->savedCallback);
    probe->savedCallback();
    EXPECT_EQ (probe->operationCount, 0);
}

TEST (MessageThreadCallGenerationTests,
      ShutdownUsesPrebuiltResultDespiteInjectedPreparationFailure)
{
    struct Probe
    {
        std::promise<void> finished;
        MessageThreadCallResult<int> result;
        std::function<void()> savedCallback;
        std::atomic<int> operationCount { 0 };
    };

    auto generation = std::make_shared<MessageThreadCallGeneration>();
    auto probe = std::make_shared<Probe>();
    auto queued = std::make_shared<std::promise<void>>();
    auto queuedFuture = queued->get_future();
    auto finishedFuture = probe->finished.get_future();
    std::thread caller (
        [probe, generation, queued]
        {
            probe->result = runDispatchedCall (
                [probe]
                {
                    ++probe->operationCount;
                    return 42;
                },
                [probe, queued] (std::function<void()> callback)
                {
                    probe->savedCallback = std::move (callback);
                    queued->set_value();
                    return true;
                },
                5s,
                *generation);
            probe->finished.set_value();
        });

    const auto queuedInTime =
        queuedFuture.wait_for (1s) == std::future_status::ready;
    MessageThreadCallDetail::clearInjectedPreparationFailures();
    MessageThreadCallDetail::failNextCompletionPreparation();
    generation->quiesce();
    const auto completedInTime =
        finishedFuture.wait_for (1s) == std::future_status::ready;
    const auto unconsumedPreparationFailures =
        MessageThreadCallDetail::injectedPreparationFailureCount();
    MessageThreadCallDetail::clearInjectedPreparationFailures();
    const auto remainingPreparationFailures =
        MessageThreadCallDetail::injectedPreparationFailureCount();

    if (completedInTime)
        caller.join();
    else
        caller.detach();

    ASSERT_TRUE (queuedInTime);
    ASSERT_TRUE (completedInTime);
    EXPECT_EQ (unconsumedPreparationFailures, 1);
    EXPECT_EQ (remainingPreparationFailures, 0);
    EXPECT_EQ (probe->result.status, MessageThreadCallStatus::dispatchFailed);
    EXPECT_EQ (
        probe->result.error,
        "Message-thread call cancelled by listener shutdown.");
    EXPECT_EQ (probe->operationCount, 0);
    ASSERT_TRUE (probe->savedCallback);
    probe->savedCallback();
    EXPECT_EQ (probe->operationCount, 0);
}

TEST (MessageThreadCallGenerationTests,
      OneQuiesceCancelsMultiplePendingCalls)
{
    struct Probe
    {
        MessageThreadCallGeneration generation;
        std::atomic<int> queuedCount { 0 };
        std::promise<void> bothQueued;
        std::promise<void> firstFinished;
        std::promise<void> secondFinished;
        std::function<void()> firstCallback;
        std::function<void()> secondCallback;
        MessageThreadCallResult<int> firstResult;
        MessageThreadCallResult<int> secondResult;
    };

    auto probe = std::make_shared<Probe>();
    auto bothQueuedFuture = probe->bothQueued.get_future();
    auto firstFinishedFuture = probe->firstFinished.get_future();
    auto secondFinishedFuture = probe->secondFinished.get_future();

    std::thread firstWaiter (
        [probe]
        {
            probe->firstResult = runDispatchedCall (
                [] { return 42; },
                [probe] (std::function<void()> callback)
                {
                    probe->firstCallback = std::move (callback);
                    if (++probe->queuedCount == 2)
                        probe->bothQueued.set_value();
                    return true;
                },
                5s,
                probe->generation);
            probe->firstFinished.set_value();
        });
    std::thread secondWaiter (
        [probe]
        {
            probe->secondResult = runDispatchedCall (
                [] { return 42; },
                [probe] (std::function<void()> callback)
                {
                    probe->secondCallback = std::move (callback);
                    if (++probe->queuedCount == 2)
                        probe->bothQueued.set_value();
                    return true;
                },
                5s,
                probe->generation);
            probe->secondFinished.set_value();
        });
    const auto bothQueuedInTime =
        bothQueuedFuture.wait_for (1s) == std::future_status::ready;
    probe->generation.quiesce();
    const auto firstCompletedInTime =
        firstFinishedFuture.wait_for (1s) == std::future_status::ready;
    const auto secondCompletedInTime =
        secondFinishedFuture.wait_for (1s) == std::future_status::ready;
    if (firstCompletedInTime)
        firstWaiter.join();
    else
        firstWaiter.detach();
    if (secondCompletedInTime)
        secondWaiter.join();
    else
        secondWaiter.detach();

    EXPECT_TRUE (bothQueuedInTime);
    ASSERT_TRUE (firstCompletedInTime);
    ASSERT_TRUE (secondCompletedInTime);
    EXPECT_EQ (probe->firstResult.status, MessageThreadCallStatus::dispatchFailed);
    EXPECT_EQ (probe->secondResult.status, MessageThreadCallStatus::dispatchFailed);
    ASSERT_TRUE (probe->firstCallback);
    ASSERT_TRUE (probe->secondCallback);
}

TEST (MessageThreadCallGenerationTests,
      StartClaimWinsOverGenerationQuiesceAndReturnsDeterminateResult)
{
    MessageThreadCallGeneration generation;
    std::promise<void> started;
    auto startedFuture = started.get_future().share();
    std::promise<void> release;
    auto releaseFuture = release.get_future().share();
    std::thread messageThread;
    std::atomic<bool> startedInTime { false };
    std::atomic<bool> releasedInTime { false };

    const auto result = runDispatchedCall (
        [&]
        {
            started.set_value();
            releasedInTime =
                releaseFuture.wait_for (1s) == std::future_status::ready;
            return 42;
        },
        [&] (std::function<void()> callback)
        {
            messageThread = std::thread (
                [callback = std::move (callback)] () mutable
                {
                    callback();
                });
            startedInTime =
                startedFuture.wait_for (1s) == std::future_status::ready;
            generation.quiesce();
            release.set_value();
            return true;
        },
        5s,
        generation);

    messageThread.join();
    EXPECT_TRUE (startedInTime);
    EXPECT_TRUE (releasedInTime);
    EXPECT_EQ (result.status, MessageThreadCallStatus::completed);
    ASSERT_TRUE (result.value.has_value());
    EXPECT_EQ (*result.value, 42);
}

TEST (MessageThreadCallGenerationTests,
      RegistrationBeforeQuiesceIsCancelledAfterExplicitSynchronization)
{
    MessageThreadCallGeneration generation;
    std::promise<void> registrationMayProceed;
    auto registrationMayProceedFuture = registrationMayProceed.get_future().share();
    std::promise<void> registered;
    auto registeredFuture = registered.get_future();
    std::atomic<int> cancellationCount { 0 };
    MessageThreadCallGeneration::Registration registration;

    std::thread registrar (
        [&]
        {
            if (registrationMayProceedFuture.wait_for (1s)
                != std::future_status::ready)
            {
                registered.set_value();
                return;
            }
            registration = generation.registerCancellation (
                [&] { ++cancellationCount; });
            registered.set_value();
        });

    registrationMayProceed.set_value();
    const auto registeredInTime =
        registeredFuture.wait_for (1s) == std::future_status::ready;
    generation.quiesce();
    registrar.join();

    EXPECT_TRUE (registeredInTime);
    EXPECT_TRUE (registration);
    EXPECT_EQ (cancellationCount, 1);
}

TEST (MessageThreadCallGenerationTests,
      QuiesceBeforeRegistrationRejectsCancellationAfterExplicitSynchronization)
{
    MessageThreadCallGeneration generation;
    std::promise<void> registrationMayProceed;
    auto registrationMayProceedFuture = registrationMayProceed.get_future().share();
    std::promise<void> registrationFinished;
    auto registrationFinishedFuture = registrationFinished.get_future();
    std::atomic<int> cancellationCount { 0 };
    MessageThreadCallGeneration::Registration registration;

    std::thread registrar (
        [&]
        {
            if (registrationMayProceedFuture.wait_for (1s)
                != std::future_status::ready)
            {
                registrationFinished.set_value();
                return;
            }
            registration = generation.registerCancellation (
                [&] { ++cancellationCount; });
            registrationFinished.set_value();
        });

    generation.quiesce();
    registrationMayProceed.set_value();
    const auto registrationFinishedInTime =
        registrationFinishedFuture.wait_for (1s) == std::future_status::ready;
    registrar.join();

    EXPECT_TRUE (registrationFinishedInTime);
    EXPECT_FALSE (registration);
    EXPECT_EQ (cancellationCount, 0);
}

TEST (MessageThreadCallGenerationTests,
      DestroyedRegistrationIsNotRetainedForLaterQuiesce)
{
    MessageThreadCallGeneration generation;
    std::atomic<int> cancellationCount { 0 };

    {
        auto registration = generation.registerCancellation (
            [&] { ++cancellationCount; });
        ASSERT_TRUE (registration);
    }

    generation.quiesce();
    EXPECT_EQ (cancellationCount, 0);
}

TEST (MessageThreadCallGenerationTests,
      CompletedDispatchedCallUnregistersBeforeLaterQuiesce)
{
    MessageThreadCallGeneration generation;
    const auto result = runDispatchedCall (
        [] { return 42; },
        [] (std::function<void()> callback)
        {
            callback();
            return true;
        },
        100ms,
        generation);

    EXPECT_EQ (result.status, MessageThreadCallStatus::completed);
    EXPECT_EQ (generation.pendingCancellationCount(), 0u);
    generation.quiesce();
    EXPECT_EQ (generation.pendingCancellationCount(), 0u);
}

TEST (MessageThreadCallGenerationTests,
      ThrowingCancellationDoesNotPreventOtherPendingCallsFromBeingCancelled)
{
    MessageThreadCallGeneration generation;
    std::atomic<int> cancellationCount { 0 };
    auto throwingRegistration = generation.registerCancellation (
        [] { throw std::runtime_error ("cancel failure"); });
    auto succeedingRegistration = generation.registerCancellation (
        [&] { ++cancellationCount; });

    ASSERT_TRUE (throwingRegistration);
    ASSERT_TRUE (succeedingRegistration);
    generation.quiesce();

    EXPECT_EQ (cancellationCount, 1);
}

TEST (MessageThreadCallGenerationTests,
      RepeatedQuiesceDoesNotRepeatCancellation)
{
    MessageThreadCallGeneration generation;
    std::atomic<int> cancellationCount { 0 };
    auto registration = generation.registerCancellation (
        [&] { ++cancellationCount; });

    ASSERT_TRUE (registration);
    generation.quiesce();
    generation.quiesce();

    EXPECT_EQ (cancellationCount, 1);
}

TEST (MessageThreadCallGenerationTests,
      AlreadyQuiescedGenerationRejectsCallWithoutDispatching)
{
    MessageThreadCallGeneration generation;
    generation.quiesce();
    bool dispatcherCalled = false;
    bool operationRan = false;
    MessageThreadCallDetail::clearInjectedPreparationFailures();
    MessageThreadCallDetail::failNextCompletionPreparation();

    const auto result = runDispatchedCall (
        [&]
        {
            operationRan = true;
            return 42;
        },
        [&] (std::function<void()>)
        {
            dispatcherCalled = true;
            return true;
        },
        100ms,
        generation);

    const auto unconsumedPreparationFailures =
        MessageThreadCallDetail::injectedPreparationFailureCount();
    MessageThreadCallDetail::clearInjectedPreparationFailures();
    const auto remainingPreparationFailures =
        MessageThreadCallDetail::injectedPreparationFailureCount();

    EXPECT_EQ (result.status, MessageThreadCallStatus::dispatchFailed);
    EXPECT_EQ (
        result.error,
        "Message-thread call cancelled by listener shutdown.");
    EXPECT_EQ (unconsumedPreparationFailures, 1);
    EXPECT_EQ (remainingPreparationFailures, 0);
    EXPECT_FALSE (dispatcherCalled);
    EXPECT_FALSE (operationRan);
}

TEST (MessageThreadCallGenerationTests,
      SavedOldGenerationCallbackCannotMutateAfterFreshGenerationIsCreated)
{
    struct Probe
    {
        MessageThreadCallGeneration oldGeneration;
        std::function<void()> savedOldCallback;
        std::promise<void> oldCallbackQueued;
        std::promise<void> oldFinished;
        std::atomic<int> operationCount { 0 };
        MessageThreadCallResult<int> oldResult;
    };

    auto probe = std::make_shared<Probe>();
    auto oldCallbackQueuedFuture = probe->oldCallbackQueued.get_future();
    auto oldFinishedFuture = probe->oldFinished.get_future();

    std::thread oldWaiter (
        [probe]
        {
            probe->oldResult = runDispatchedCall (
                [probe]
                {
                    ++probe->operationCount;
                    return 42;
                },
                [probe] (std::function<void()> callback)
                {
                    probe->savedOldCallback = std::move (callback);
                    probe->oldCallbackQueued.set_value();
                    return true;
                },
                5s,
                probe->oldGeneration);
            probe->oldFinished.set_value();
        });

    const auto oldQueuedInTime =
        oldCallbackQueuedFuture.wait_for (1s) == std::future_status::ready;
    probe->oldGeneration.quiesce();
    const auto oldCompletedInTime =
        oldFinishedFuture.wait_for (1s) == std::future_status::ready;
    if (oldCompletedInTime)
        oldWaiter.join();
    else
        oldWaiter.detach();

    ASSERT_TRUE (oldQueuedInTime);
    ASSERT_TRUE (oldCompletedInTime);
    MessageThreadCallGeneration freshGeneration;
    probe->savedOldCallback();

    const auto freshResult = runDispatchedCall (
        [] { return 7; },
        [] (std::function<void()> callback)
        {
            callback();
            return true;
        },
        100ms,
        freshGeneration);

    EXPECT_EQ (probe->oldResult.status, MessageThreadCallStatus::dispatchFailed);
    EXPECT_EQ (probe->operationCount, 0);
    EXPECT_EQ (freshResult.status, MessageThreadCallStatus::completed);
    ASSERT_TRUE (freshResult.value.has_value());
    EXPECT_EQ (*freshResult.value, 7);
}

TEST (MessageThreadCallGenerationTests,
      DispatcherFailuresAndLocalDeadlineKeepTheirExistingMappings)
{
    MessageThreadCallGeneration generation;
    const auto rejected = runDispatchedCall (
        [] { return 42; },
        [] (std::function<void()>) { return false; },
        5s,
        generation);
    EXPECT_EQ (rejected.status, MessageThreadCallStatus::dispatchFailed);

    const auto thrown = runDispatchedCall (
        [] { return 42; },
        [] (std::function<void()>) -> bool
        {
            throw std::runtime_error ("dispatcher failure");
        },
        5s,
        generation);
    EXPECT_EQ (thrown.status, MessageThreadCallStatus::dispatchFailed);
    EXPECT_TRUE (thrown.error.contains ("dispatcher failure"));

    std::function<void()> pendingCallback;
    const auto timedOut = runDispatchedCall (
        [] { return 42; },
        [&] (std::function<void()> callback)
        {
            pendingCallback = std::move (callback);
            return true;
        },
        0ms,
        generation);
    EXPECT_EQ (timedOut.status, MessageThreadCallStatus::timedOut);
    ASSERT_TRUE (pendingCallback);
}
