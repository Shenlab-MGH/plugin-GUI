#include "../../Source/Utils/RecordingOptionsControl.h"
#include "gtest/gtest.h"

#include <chrono>
#include <atomic>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <thread>

using namespace std::chrono_literals;

namespace
{
struct PendingRecordingOptionsState
{
    MessageThreadCallGeneration generation;
    std::function<void()> pendingOperation;
    std::promise<void> queuedPromise;
    std::promise<RecordingOptionsControlResult> resultPromise;
    std::atomic<int> applyCount { 0 };
};
} // namespace

TEST (RecordingOptionsControlTests, AppliesValidatedUpdatesThroughTheDispatcher)
{
    RecordingOptionsUpdate applied;
    const auto result = handleRecordingOptionsPut (
        R"({"expanded":true,"force_new_directory":false})",
        [] (std::function<void()> operation)
        {
            operation();
            return true;
        },
        [&] (const RecordingOptionsUpdate& update)
        {
            applied = update;
            RecordingOptionsStatus status;
            status.expanded = *update.expanded;
            status.forceNewDirectory = *update.forceNewDirectory;
            return RecordingOptionsApplyResult { status, {}, {} };
        },
        50ms);

    EXPECT_EQ (result.httpStatus, 200);
    ASSERT_TRUE (result.status.has_value());
    EXPECT_TRUE (result.status->expanded);
    EXPECT_FALSE (result.status->forceNewDirectory);
    ASSERT_TRUE (applied.expanded.has_value());
    EXPECT_TRUE (*applied.expanded);
}

TEST (RecordingOptionsControlTests, RejectsInvalidRequestsBeforeDispatch)
{
    bool dispatched = false;
    bool applied = false;
    const auto result = handleRecordingOptionsPut (
        R"({"expanded":"yes"})",
        [&] (std::function<void()>)
        {
            dispatched = true;
            return true;
        },
        [&] (const RecordingOptionsUpdate&)
        {
            applied = true;
            return RecordingOptionsApplyResult { RecordingOptionsStatus {}, {}, {} };
        },
        50ms);

    EXPECT_EQ (result.httpStatus, 400);
    EXPECT_EQ (result.errorCode, "invalid_request");
    EXPECT_FALSE (result.status.has_value());
    EXPECT_FALSE (dispatched);
    EXPECT_FALSE (applied);
}

TEST (RecordingOptionsControlTests, ReportsUnavailableAndTimedOutDispatch)
{
    auto result = handleRecordingOptionsPut (
        R"({"expanded":true})",
        [] (std::function<void()>) { return false; },
        [] (const RecordingOptionsUpdate&)
        { return RecordingOptionsApplyResult { RecordingOptionsStatus {}, {}, {} }; },
        50ms);

    EXPECT_EQ (result.httpStatus, 503);
    EXPECT_EQ (result.errorCode, "operation_unavailable");

    std::function<void()> pendingOperation;
    result = handleRecordingOptionsPut (
        R"({"expanded":true})",
        [&] (std::function<void()> operation)
        {
            pendingOperation = std::move (operation);
            return true;
        },
        [] (const RecordingOptionsUpdate&)
        { return RecordingOptionsApplyResult { RecordingOptionsStatus {}, {}, {} }; },
        1ms);

    EXPECT_EQ (result.httpStatus, 504);
    EXPECT_EQ (result.errorCode, "operation_timeout");
    ASSERT_TRUE (pendingOperation);
    pendingOperation();
}

TEST (RecordingOptionsControlTests, RejectsNewDirectoryChangesWhenTheGuiControlIsDisabled)
{
    bool newDirectorySetterCalled = false;
    RecordingOptionsStatus currentStatus;
    currentStatus.newDirectoryRequested = true;
    currentStatus.newDirectoryRequestAvailable = false;

    const auto result = handleRecordingOptionsPut (
        R"({"new_directory_requested":false})",
        [] (std::function<void()> operation)
        {
            operation();
            return true;
        },
        [&] (const RecordingOptionsUpdate& update)
        {
            return applyRecordingOptionsUpdate (
                update,
                [&] { return currentStatus; },
                [] (bool) {},
                [] (bool) {},
                [&] (bool) { newDirectorySetterCalled = true; });
        },
        50ms);

    EXPECT_EQ (result.httpStatus, 409);
    EXPECT_EQ (result.errorCode, "operation_not_available");
    EXPECT_FALSE (result.status.has_value());
    EXPECT_FALSE (newDirectorySetterCalled);
}

TEST (RecordingOptionsControlTests, GenerationQuiesceCancelsPendingRecordingOptionsUpdate)
{
    auto state = std::make_shared<PendingRecordingOptionsState>();
    const auto weakState = std::weak_ptr<PendingRecordingOptionsState> (state);
    auto queued = state->queuedPromise.get_future();
    auto resultFuture = state->resultPromise.get_future();

    std::thread caller (
        [state, weakState]
        {
            try
            {
                state->resultPromise.set_value (handleRecordingOptionsPut (
                    R"({"expanded":true,"force_new_directory":false})",
                    [state] (std::function<void()> operation)
                    {
                        state->pendingOperation = std::move (operation);
                        state->queuedPromise.set_value();
                        return true;
                    },
                    [weakState] (const RecordingOptionsUpdate& update)
                    {
                        if (auto lockedState = weakState.lock())
                            ++lockedState->applyCount;

                        RecordingOptionsStatus status;
                        status.expanded = *update.expanded;
                        status.forceNewDirectory = *update.forceNewDirectory;
                        return RecordingOptionsApplyResult { status, {}, {} };
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
    EXPECT_EQ (state->applyCount.load(), 0);

    if (! queuedInTime)
        return;

    auto lateOperation = std::move (state->pendingOperation);
    EXPECT_TRUE (lateOperation);
    EXPECT_FALSE (state->pendingOperation);

    if (lateOperation)
    {
        lateOperation();
        EXPECT_EQ (state->applyCount.load(), 0);
    }
}
