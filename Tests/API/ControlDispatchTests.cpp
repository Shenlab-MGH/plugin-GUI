#include "../../Source/Utils/ControlDispatch.h"
#include "gtest/gtest.h"

#include <chrono>
#include <functional>
#include <stdexcept>

using namespace std::chrono_literals;

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
