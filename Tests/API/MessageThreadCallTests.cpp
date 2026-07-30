#include "../../Source/Utils/MessageThreadCall.h"
#include "gtest/gtest.h"

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
