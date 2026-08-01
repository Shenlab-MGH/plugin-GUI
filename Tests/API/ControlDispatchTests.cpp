#include "../../Source/Utils/ControlDispatch.h"
#include "gtest/gtest.h"

#include <chrono>
#include <atomic>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;

namespace
{
struct PendingDispatchState
{
    MessageThreadCallGeneration generation;
    std::function<void()> pendingOperation;
    std::promise<void> queuedPromise;
    std::promise<ControlDispatchResult<int>> resultPromise;
    std::atomic<int> operationCount { 0 };
};
} // namespace

TEST (ControlDispatchTests, ReturnsAnOwnedValueFromTheMessageThread)
{
    const auto result = handleControlDispatch (
        [] (std::function<void()> operation)
        {
            operation();
            return true;
        },
        [] { return String ("saved configuration"); },
        50ms);

    EXPECT_EQ (result.httpStatus, 200);
    ASSERT_TRUE (result.value.has_value());
    EXPECT_EQ (*result.value, "saved configuration");
}

TEST (ControlDispatchTests, MapsUnavailableTimeoutAndFailuresToLegacyErrors)
{
    auto result = handleControlDispatch (
        [] (std::function<void()>) { return false; },
        [] { return 1; },
        50ms);
    EXPECT_EQ (result.httpStatus, 503);
    EXPECT_EQ (result.errorCode, "operation_unavailable");

    std::function<void()> pendingOperation;
    result = handleControlDispatch (
        [&] (std::function<void()> operation)
        {
            pendingOperation = std::move (operation);
            return true;
        },
        [] { return 1; },
        1ms);
    EXPECT_EQ (result.httpStatus, 504);
    EXPECT_EQ (result.errorCode, "operation_timeout");
    ASSERT_TRUE (pendingOperation);
    pendingOperation();

    result = handleControlDispatch (
        [] (std::function<void()> operation)
        {
            operation();
            return true;
        },
        []() -> int { throw std::runtime_error ("dispatch operation failed"); },
        50ms);
    EXPECT_EQ (result.httpStatus, 500);
    EXPECT_EQ (result.errorCode, "operation_failed");
    EXPECT_TRUE (result.errorMessage.contains ("dispatch operation failed"));
}

TEST (ControlDispatchTests, GenerationQuiesceCancelsPendingDispatchAndLateCallbackCannotMutate)
{
    auto state = std::make_shared<PendingDispatchState>();
    const auto weakState = std::weak_ptr<PendingDispatchState> (state);
    auto queued = state->queuedPromise.get_future();
    auto resultFuture = state->resultPromise.get_future();

    std::thread caller (
        [state, weakState]
        {
            try
            {
                state->resultPromise.set_value (handleControlDispatch (
                    [state] (std::function<void()> operation)
                    {
                        state->pendingOperation = std::move (operation);
                        state->queuedPromise.set_value();
                        return true;
                    },
                    [weakState]
                    {
                        if (auto lockedState = weakState.lock())
                            ++lockedState->operationCount;

                        return 7;
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
    EXPECT_EQ (state->operationCount.load(), 0);

    if (! queuedInTime)
        return;

    auto lateOperation = std::move (state->pendingOperation);
    EXPECT_TRUE (lateOperation);
    EXPECT_FALSE (state->pendingOperation);

    if (lateOperation)
    {
        lateOperation();
        EXPECT_EQ (state->operationCount.load(), 0);
    }
}
