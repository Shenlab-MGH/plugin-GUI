#include "../../Source/Utils/ControlRead.h"
#include "gtest/gtest.h"

#include <chrono>
#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;

namespace
{
struct PendingReadState
{
    MessageThreadCallGeneration generation;
    std::function<void()> pendingOperation;
    std::promise<void> queuedPromise;
    std::promise<ControlReadResult<int>> resultPromise;
    std::atomic<int> readCount { 0 };
};

struct StartedReadState
{
    StartedReadState()
        : release (releasePromise.get_future().share())
    {
    }

    MessageThreadCallGeneration generation;
    std::promise<void> startedPromise;
    std::promise<void> releasePromise;
    std::shared_future<void> release;
    std::promise<ControlReadResult<int>> resultPromise;
    std::atomic<int> readCount { 0 };
};
} // namespace

TEST (ControlReadTests, ReturnsStateReadThroughTheDispatcher)
{
    const auto result = handleControlRead (
        [] (std::function<void()> operation)
        {
            operation();
            return true;
        },
        [] { return 42; },
        50ms);

    EXPECT_EQ (result.httpStatus, 200);
    ASSERT_TRUE (result.value.has_value());
    EXPECT_EQ (*result.value, 42);
}

TEST (ControlReadTests, ReportsDispatchFailureTimeoutAndReadFailure)
{
    auto result = handleControlRead (
        [] (std::function<void()>) { return false; },
        [] { return 42; },
        50ms);
    EXPECT_EQ (result.httpStatus, 503);
    EXPECT_EQ (result.errorCode, "operation_unavailable");

    std::function<void()> pendingOperation;
    result = handleControlRead (
        [&] (std::function<void()> operation)
        {
            pendingOperation = std::move (operation);
            return true;
        },
        [] { return 42; },
        1ms);
    EXPECT_EQ (result.httpStatus, 504);
    EXPECT_EQ (result.errorCode, "operation_timeout");
    ASSERT_TRUE (pendingOperation);
    pendingOperation();

    result = handleControlRead (
        [] (std::function<void()> operation)
        {
            operation();
            return true;
        },
        []() -> int { throw std::runtime_error ("read failed"); },
        50ms);
    EXPECT_EQ (result.httpStatus, 500);
    EXPECT_EQ (result.errorCode, "operation_failed");
    EXPECT_TRUE (result.errorMessage.contains ("read failed"));
}

TEST (ControlReadTests, GenerationQuiesceCancelsPendingReadAndLateCallbackIsNoOp)
{
    auto state = std::make_shared<PendingReadState>();
    const auto weakState = std::weak_ptr<PendingReadState> (state);
    auto queued = state->queuedPromise.get_future();
    auto resultFuture = state->resultPromise.get_future();

    std::thread caller (
        [state, weakState]
        {
            try
            {
                state->resultPromise.set_value (handleControlRead (
                    [state] (std::function<void()> operation)
                    {
                        state->pendingOperation = std::move (operation);
                        state->queuedPromise.set_value();
                        return true;
                    },
                    [weakState]
                    {
                        if (auto lockedState = weakState.lock())
                            ++lockedState->readCount;

                        return 42;
                    },
                    5s,
                    state->generation));
            }
            catch (...)
            {
                state->resultPromise.set_exception (std::current_exception());
            }
        });

    const auto queuedInTime = queued.wait_for (1s) == std::future_status::ready;
    state->generation.quiesce();
    const auto resultReady = resultFuture.wait_for (1s) == std::future_status::ready;

    EXPECT_TRUE (queuedInTime);
    EXPECT_TRUE (resultReady);

    if (! resultReady)
    {
        caller.detach();
        return;
    }

    caller.join();
    const auto result = resultFuture.get();
    EXPECT_EQ (result.httpStatus, 503);
    EXPECT_EQ (result.errorCode, "operation_unavailable");
    EXPECT_EQ (state->readCount.load(), 0);

    if (! queuedInTime)
        return;

    auto lateOperation = std::move (state->pendingOperation);
    EXPECT_TRUE (lateOperation);
    EXPECT_FALSE (state->pendingOperation);

    if (lateOperation)
    {
        lateOperation();
        EXPECT_EQ (state->readCount.load(), 0);
    }
}

TEST (ControlReadTests, StartedReadWinsGenerationQuiesceAndReturnsValue)
{
    auto state = std::make_shared<StartedReadState>();
    auto started = state->startedPromise.get_future();
    auto resultFuture = state->resultPromise.get_future();

    std::thread caller (
        [state]
        {
            try
            {
                state->resultPromise.set_value (handleControlRead (
                    [state] (std::function<void()> operation)
                    {
                        operation();
                        return true;
                    },
                    [state]
                    {
                        ++state->readCount;
                        state->startedPromise.set_value();
                        state->release.wait();
                        return 42;
                    },
                    5s,
                    state->generation));
            }
            catch (...)
            {
                state->resultPromise.set_exception (std::current_exception());
            }
        });

    const auto startedInTime = started.wait_for (1s) == std::future_status::ready;
    state->generation.quiesce();
    const auto callerStillPending =
        resultFuture.wait_for (50ms) == std::future_status::timeout;

    state->releasePromise.set_value();
    const auto resultReady = resultFuture.wait_for (1s) == std::future_status::ready;

    EXPECT_TRUE (startedInTime);
    EXPECT_TRUE (callerStillPending);
    EXPECT_TRUE (resultReady);

    if (! resultReady)
    {
        caller.detach();
        return;
    }

    caller.join();
    const auto result = resultFuture.get();
    EXPECT_EQ (result.httpStatus, 200);
    EXPECT_TRUE (result.value.has_value());

    if (result.value.has_value())
        EXPECT_EQ (*result.value, 42);

    EXPECT_EQ (state->readCount.load(), 1);
}

TEST (ControlReadTests, ReadsTheDirectoryOnTheMessageThreadButMeasuresDiskUsageOnTheCaller)
{
    bool insideDispatchedOperation = false;
    bool directoryReadOnMessageThread = false;
    bool measurementRanOnMessageThread = true;

    const auto result = handleRecordingDiskUsageRead (
        [&] (std::function<void()> operation)
        {
            insideDispatchedOperation = true;
            operation();
            insideDispatchedOperation = false;
            return true;
        },
        [&]
        {
            directoryReadOnMessageThread = insideDispatchedOperation;
            return 7;
        },
        [&] (int directory)
        {
            measurementRanOnMessageThread = insideDispatchedOperation;
            return static_cast<float> (directory) / 10.0f;
        },
        50ms);

    EXPECT_EQ (result.httpStatus, 200);
    ASSERT_TRUE (result.value.has_value());
    EXPECT_FLOAT_EQ (*result.value, 0.7f);
    EXPECT_TRUE (directoryReadOnMessageThread);
    EXPECT_FALSE (measurementRanOnMessageThread);
}
