#include "../../Source/Utils/ControlRead.h"
#include "gtest/gtest.h"

#include <chrono>
#include <functional>
#include <stdexcept>

using namespace std::chrono_literals;

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
