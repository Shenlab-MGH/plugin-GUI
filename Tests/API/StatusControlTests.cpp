#include "../../Source/Utils/StatusControl.h"
#include "gtest/gtest.h"

#include <chrono>
#include <functional>
#include <vector>

using namespace std::chrono_literals;

namespace
{
struct FakeStatusControl
{
    StatusMode mode = StatusMode::Idle;
    bool synchronized = true;
    bool inDispatchedOperation = false;
    bool allCallsWereDispatched = true;
    int mutationCalls = 0;

    StatusControlOperations operations()
    {
        return {
            [this]
            {
                allCallsWereDispatched &= inDispatchedOperation;
                return mode;
            },
            [this] (StatusMode requested)
            {
                allCallsWereDispatched &= inDispatchedOperation;
                ++mutationCalls;
                mode = requested;
            },
            [this]
            {
                allCallsWereDispatched &= inDispatchedOperation;
                return synchronized;
            }
        };
    }

    bool dispatch (std::function<void()> operation)
    {
        inDispatchedOperation = true;
        operation();
        inDispatchedOperation = false;
        return true;
    }
};
} // namespace

TEST (StatusControlTests, RejectsUnsynchronizedRecordWithoutMutationInsideDispatchedOperation)
{
    FakeStatusControl control;
    control.mode = StatusMode::Acquire;
    control.synchronized = false;

    const auto result = requestStatusTransition (
        StatusMode::Record,
        control.operations(),
        [&control] (std::function<void()> operation)
        { return control.dispatch (std::move (operation)); },
        50ms);

    EXPECT_EQ (result.mode, StatusMode::Acquire);
    EXPECT_EQ (result.failure, StatusTransitionFailure::recordNodesNotSynchronized);
    EXPECT_EQ (control.mutationCalls, 0);
    EXPECT_TRUE (control.allCallsWereDispatched);
}

TEST (StatusControlTests, PreservesSynchronizedTransitionsStopsAndIdempotence)
{
    struct TransitionCase
    {
        StatusMode initial;
        StatusMode requested;
        int expectedMutations;
    };
    const std::vector<TransitionCase> cases {
        { StatusMode::Idle, StatusMode::Record, 1 },
        { StatusMode::Acquire, StatusMode::Record, 1 },
        { StatusMode::Record, StatusMode::Record, 0 },
        { StatusMode::Record, StatusMode::Acquire, 1 },
        { StatusMode::Record, StatusMode::Idle, 1 }
    };

    for (const auto& transition : cases)
    {
        FakeStatusControl control;
        control.mode = transition.initial;

        const auto result = requestStatusTransition (
            transition.requested,
            control.operations(),
            [&control] (std::function<void()> operation)
            { return control.dispatch (std::move (operation)); },
            50ms);

        EXPECT_EQ (result.mode, transition.requested);
        EXPECT_EQ (result.failure, StatusTransitionFailure::none);
        EXPECT_EQ (control.mutationCalls, transition.expectedMutations);
        EXPECT_TRUE (control.allCallsWereDispatched);
    }
}

TEST (StatusControlTests, ReportsDispatchFailureAndCancelsTimedOutMutation)
{
    FakeStatusControl unavailable;
    const auto unavailableResult = requestStatusTransition (
        StatusMode::Acquire,
        unavailable.operations(),
        [] (std::function<void()>) { return false; },
        50ms);
    EXPECT_EQ (unavailableResult.failure, StatusTransitionFailure::operationUnavailable);
    EXPECT_EQ (unavailable.mutationCalls, 0);

    FakeStatusControl timedOut;
    std::function<void()> pendingOperation;
    const auto timedOutResult = requestStatusTransition (
        StatusMode::Acquire,
        timedOut.operations(),
        [&pendingOperation] (std::function<void()> operation)
        {
            pendingOperation = std::move (operation);
            return true;
        },
        1ms);
    EXPECT_EQ (timedOutResult.failure, StatusTransitionFailure::operationTimedOut);
    ASSERT_TRUE (pendingOperation);

    pendingOperation();
    EXPECT_EQ (timedOut.mutationCalls, 0);
}
