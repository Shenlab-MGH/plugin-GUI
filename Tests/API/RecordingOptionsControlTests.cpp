#include "../../Source/Utils/RecordingOptionsControl.h"
#include "gtest/gtest.h"

#include <chrono>
#include <functional>

using namespace std::chrono_literals;

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
