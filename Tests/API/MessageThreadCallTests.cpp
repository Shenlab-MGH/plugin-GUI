#include "../../Source/Utils/MessageThreadCall.h"
#include "gtest/gtest.h"

#include <chrono>
#include <functional>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;

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

TEST (MessageThreadCallTests, WaitsForAnOperationThatAlreadyStartedInsteadOfReportingALateTimeout)
{
    const auto result = runDispatchedCall (
        []
        {
            std::this_thread::sleep_for (10ms);
            return 42;
        },
        [] (std::function<void()> operation)
        {
            std::thread ([operation = std::move (operation)]() mutable
                         { operation(); })
                .detach();
            return true;
        },
        5ms);

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
