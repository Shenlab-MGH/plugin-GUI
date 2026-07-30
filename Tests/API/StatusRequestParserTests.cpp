#include "../../Source/Utils/ControlStatusJson.h"

#include "gtest/gtest.h"

#include <initializer_list>

namespace
{
using Mode = AcquisitionRecordingMode;

void expectInvalidStatusRequest (StringRef body)
{
    const auto result = parseStatusRequest (body);

    EXPECT_FALSE (result.request.has_value())
        << String (body);
    EXPECT_EQ (result.errorCode,
               "invalid_request")
        << String (body);
    EXPECT_TRUE (result.error.isNotEmpty())
        << String (body);
}

void expectInvalidStatusRequests (
    std::initializer_list<const char*> bodies)
{
    for (const auto* body : bodies)
        expectInvalidStatusRequest (body);
}
} // namespace

TEST (StatusRequestParserTests,
      AcceptsOnlyTheThreeExactModes)
{
    auto result =
        parseStatusRequest (R"({"mode":"IDLE"})");
    ASSERT_TRUE (result.request.has_value());
    EXPECT_EQ (result.request->mode, Mode::idle);
    EXPECT_FALSE (
        result.request->confirmUnsynchronized);
    EXPECT_TRUE (result.errorCode.isEmpty());
    EXPECT_TRUE (result.error.isEmpty());

    result =
        parseStatusRequest (R"({"mode":"ACQUIRE"})");
    ASSERT_TRUE (result.request.has_value());
    EXPECT_EQ (result.request->mode, Mode::acquire);
    EXPECT_FALSE (
        result.request->confirmUnsynchronized);

    result =
        parseStatusRequest (R"({"mode":"RECORD"})");
    ASSERT_TRUE (result.request.has_value());
    EXPECT_EQ (result.request->mode, Mode::record);
    EXPECT_FALSE (
        result.request->confirmUnsynchronized);
}

TEST (StatusRequestParserTests,
      ParsesRecordConfirmationAsRequestLocalPermission)
{
    auto result = parseStatusRequest (
        R"({"mode":"RECORD","confirm_unsynchronized":false})");
    ASSERT_TRUE (result.request.has_value());
    EXPECT_EQ (result.request->mode, Mode::record);
    EXPECT_FALSE (
        result.request->confirmUnsynchronized);

    result = parseStatusRequest (
        R"({"confirm_unsynchronized":true,"mode":"RECORD"})");
    ASSERT_TRUE (result.request.has_value());
    EXPECT_EQ (result.request->mode, Mode::record);
    EXPECT_TRUE (
        result.request->confirmUnsynchronized);
}

TEST (StatusRequestParserTests,
      RejectsMalformedTrailingAndNonObjectJson)
{
    expectInvalidStatusRequests ({
        "",
        "{",
        R"({"mode":"IDLE"} trailing)",
        "null",
        "[]",
        R"("RECORD")",
        "1",
        "true"
    });
}

TEST (StatusRequestParserTests,
      RejectsMissingEmptyAndWrongTypeModes)
{
    expectInvalidStatusRequests ({
        "{}",
        R"({"confirm_unsynchronized":true})",
        R"({"mode":null})",
        R"({"mode":true})",
        R"({"mode":1})",
        R"({"mode":[]})",
        R"({"mode":{}})",
        R"({"mode":""})"
    });
}

TEST (StatusRequestParserTests,
      RejectsUnknownAndNonExactModeNames)
{
    expectInvalidStatusRequests ({
        R"({"mode":"idle"})",
        R"({"mode":"Acquire"})",
        R"({"mode":"record"})",
        R"({"mode":" IDLE"})",
        R"({"mode":"IDLE "})",
        R"({"mode":"PAUSE"})",
        R"({"mode":"IDLE","force":true})"
    });
}

TEST (StatusRequestParserTests,
      RejectsWrongConfirmationTypes)
{
    expectInvalidStatusRequests ({
        R"({"mode":"RECORD","confirm_unsynchronized":null})",
        R"({"mode":"RECORD","confirm_unsynchronized":"true"})",
        R"({"mode":"RECORD","confirm_unsynchronized":1})",
        R"({"mode":"RECORD","confirm_unsynchronized":[]})",
        R"({"mode":"RECORD","confirm_unsynchronized":{}})"
    });
}

TEST (StatusRequestParserTests,
      RejectsConfirmationForIdleAndAcquireEvenWhenFalse)
{
    expectInvalidStatusRequests ({
        R"({"mode":"IDLE","confirm_unsynchronized":false})",
        R"({"mode":"IDLE","confirm_unsynchronized":true})",
        R"({"mode":"ACQUIRE","confirm_unsynchronized":false})",
        R"({"mode":"ACQUIRE","confirm_unsynchronized":true})"
    });
}

TEST (StatusRequestParserTests,
      RejectsDuplicateFieldsInsteadOfSilentlyUsingOne)
{
    expectInvalidStatusRequests ({
        R"({"mode":"IDLE","mode":"RECORD"})",
        R"({"mode":"RECORD","confirm_unsynchronized":false,"confirm_unsynchronized":true})"
    });
}
