#include "../../Source/Utils/ControlStatusJson.h"
#include "gtest/gtest.h"

TEST (ControlStatusJsonTests, ParsesSupportedRecordingOptionFields)
{
    const auto result = parseRecordingOptionsUpdate (
        R"({"expanded":true,"force_new_directory":false,"new_directory_requested":true})");

    ASSERT_TRUE (result.update.has_value());
    EXPECT_TRUE (result.error.isEmpty());
    ASSERT_TRUE (result.update->expanded.has_value());
    ASSERT_TRUE (result.update->forceNewDirectory.has_value());
    ASSERT_TRUE (result.update->newDirectoryRequested.has_value());
    EXPECT_TRUE (*result.update->expanded);
    EXPECT_FALSE (*result.update->forceNewDirectory);
    EXPECT_TRUE (*result.update->newDirectoryRequested);
}

TEST (ControlStatusJsonTests, RejectsMalformedOrNonObjectRequests)
{
    auto result = parseRecordingOptionsUpdate ("{");
    EXPECT_FALSE (result.update.has_value());
    EXPECT_TRUE (result.error.isNotEmpty());

    result = parseRecordingOptionsUpdate ("[]");
    EXPECT_FALSE (result.update.has_value());
    EXPECT_TRUE (result.error.contains ("object"));
}

TEST (ControlStatusJsonTests, RejectsUnknownEmptyAndNonBooleanFields)
{
    auto result = parseRecordingOptionsUpdate (R"({"expanded":"yes"})");
    EXPECT_FALSE (result.update.has_value());
    EXPECT_TRUE (result.error.contains ("boolean"));

    result = parseRecordingOptionsUpdate (R"({"expaned":true})");
    EXPECT_FALSE (result.update.has_value());
    EXPECT_TRUE (result.error.contains ("Unknown"));

    result = parseRecordingOptionsUpdate ("{}");
    EXPECT_FALSE (result.update.has_value());
    EXPECT_TRUE (result.error.contains ("at least one"));
}

TEST (ControlStatusJsonTests, RejectsContradictoryForcedDirectoryState)
{
    const auto result = parseRecordingOptionsUpdate (
        R"({"force_new_directory":true,"new_directory_requested":false})");

    EXPECT_FALSE (result.update.has_value());
    EXPECT_TRUE (result.error.contains ("new_directory_requested"));
}
