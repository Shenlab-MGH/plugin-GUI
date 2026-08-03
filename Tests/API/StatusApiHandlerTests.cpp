#include "../../Source/Utils/StatusApiHandler.h"
#include "gtest/gtest.h"

#include <string>
#include <vector>

namespace
{
using json = nlohmann::json;

struct FakeStatus
{
    StatusMode mode = StatusMode::Idle;
    int transitionCalls = 0;
    std::vector<StatusMode> requestedModes;
    bool rejectTransition = false;
    bool recordNodesUnsynchronized = false;
    StatusTransitionFailure operationFailure = StatusTransitionFailure::none;

    StatusApiHandlers handlers()
    {
        return {
            [this] { return mode; },
            [this] (StatusMode requested)
            {
                ++transitionCalls;
                requestedModes.push_back (requested);
                if (recordNodesUnsynchronized && requested == StatusMode::Record)
                    return StatusTransitionResult {
                        mode, StatusTransitionFailure::recordNodesNotSynchronized
                    };
                if (operationFailure != StatusTransitionFailure::none)
                    return StatusTransitionResult { mode, operationFailure };
                if (rejectTransition)
                    return StatusTransitionResult {
                        mode, StatusTransitionFailure::rejected
                    };

                mode = requested;
                return StatusTransitionResult { mode, StatusTransitionFailure::none };
            }
        };
    }
};

httplib::Response put (FakeStatus& status, const std::string& body)
{
    httplib::Request request;
    request.body = body;
    httplib::Response response;
    handleStatusPut (request, response, status.handlers());
    return response;
}
json responseJson (const httplib::Response& response)
{
    return json::parse (response.body);
}

void expectJson (const httplib::Response& response, int status)
{
    EXPECT_EQ (response.status, status);
    EXPECT_EQ (response.get_header_value ("Content-Type"), "application/json");
    EXPECT_TRUE (json::accept (response.body));
}
} // namespace

TEST (StatusApiHandlerTests, GetReturnsEachActualMode)
{
    const std::vector<std::pair<StatusMode, std::string>> cases {
        { StatusMode::Idle, "IDLE" },
        { StatusMode::Acquire, "ACQUIRE" },
        { StatusMode::Record, "RECORD" }
    };

    for (const auto& [mode, expected] : cases)
    {
        FakeStatus status;
        status.mode = mode;
        httplib::Request request;
        httplib::Response response;

        handleStatusGet (request, response, status.handlers());

        expectJson (response, 200);
        EXPECT_EQ (responseJson (response), json ({ { "mode", expected } }));
        EXPECT_EQ (status.transitionCalls, 0);
    }
}

TEST (StatusApiHandlerTests, RejectsMalformedJsonBeforeMutation)
{
    FakeStatus status;
    const auto response = put (status, "{");

    expectJson (response, 400);
    EXPECT_EQ (responseJson (response)["error"]["code"], "invalid_json");
    EXPECT_EQ (status.transitionCalls, 0);
    EXPECT_EQ (status.mode, StatusMode::Idle);
}

TEST (StatusApiHandlerTests, RejectsMissingOrNonStringModeBeforeMutation)
{
    const std::vector<std::string> bodies {
        "[]", "null", "\"ACQUIRE\"", "{}", R"({"mode":null})",
        R"({"mode":true})", R"({"mode":1})", R"({"mode":[]})", R"({"mode":{}})"
    };

    for (const auto& body : bodies)
    {
        FakeStatus status;
        const auto response = put (status, body);

        expectJson (response, 400);
        EXPECT_EQ (responseJson (response)["error"]["code"], "invalid_status_request") << body;
        EXPECT_EQ (status.transitionCalls, 0) << body;
        EXPECT_EQ (status.mode, StatusMode::Idle) << body;
    }
}

TEST (StatusApiHandlerTests, RejectsUnsupportedModeBeforeMutation)
{
    const std::vector<std::string> modes { "", "acquire", "PAUSE" };

    for (const auto& mode : modes)
    {
        FakeStatus status;
        const auto response = put (status, json ({ { "mode", mode } }).dump());

        expectJson (response, 400);
        EXPECT_EQ (responseJson (response)["error"]["code"], "unsupported_mode") << mode;
        EXPECT_EQ (status.transitionCalls, 0) << mode;
        EXPECT_EQ (status.mode, StatusMode::Idle) << mode;
    }
}

TEST (StatusApiHandlerTests, AcceptsExactModesAndExtraFields)
{
    const std::vector<std::pair<std::string, StatusMode>> cases {
        { "IDLE", StatusMode::Idle },
        { "ACQUIRE", StatusMode::Acquire },
        { "RECORD", StatusMode::Record }
    };

    for (const auto& [mode, expected] : cases)
    {
        FakeStatus status;
        const auto response = put (
            status,
            json ({ { "mode", mode }, { "ignored_for_compatibility", true } }).dump());

        expectJson (response, 200);
        EXPECT_EQ (responseJson (response), json ({ { "mode", mode } }));
        ASSERT_EQ (status.requestedModes.size(), (size_t) 1);
        EXPECT_EQ (status.requestedModes[0], expected);
    }
}

TEST (StatusApiHandlerTests, ReturnsActualModeAcrossCoreTransitionMatrix)
{
    struct TransitionCase
    {
        StatusMode initial;
        const char* requested;
        StatusMode expected;
    };

    const std::vector<TransitionCase> cases {
        { StatusMode::Idle, "ACQUIRE", StatusMode::Acquire },
        { StatusMode::Idle, "RECORD", StatusMode::Record },
        { StatusMode::Record, "ACQUIRE", StatusMode::Acquire },
        { StatusMode::Acquire, "IDLE", StatusMode::Idle },
        { StatusMode::Record, "IDLE", StatusMode::Idle }
    };

    for (const auto& transition : cases)
    {
        FakeStatus status;
        status.mode = transition.initial;

        const auto response = put (
            status,
            json ({ { "mode", transition.requested } }).dump());

        expectJson (response, 200);
        EXPECT_EQ (responseJson (response)["mode"], transition.requested);
        EXPECT_EQ (status.mode, transition.expected);
    }
}

TEST (StatusApiHandlerTests, PreservesSuccessfulSameModeRequest)
{
    FakeStatus status;
    status.mode = StatusMode::Acquire;

    const auto response = put (status, R"({"mode":"ACQUIRE"})");

    expectJson (response, 200);
    EXPECT_EQ (responseJson (response), json ({ { "mode", "ACQUIRE" } }));
}

TEST (StatusApiHandlerTests, ReturnsConflictWithRequestedAndActualModesWhenRejected)
{
    struct RejectionCase
    {
        StatusMode actual;
        const char* actualText;
        const char* requested;
    };

    const std::vector<RejectionCase> cases {
        { StatusMode::Acquire, "ACQUIRE", "RECORD" },
        { StatusMode::Idle, "IDLE", "ACQUIRE" }
    };

    for (const auto& rejection : cases)
    {
        FakeStatus status;
        status.mode = rejection.actual;
        status.rejectTransition = true;

        const auto response = put (
            status,
            json ({ { "mode", rejection.requested } }).dump());

        expectJson (response, 409);
        const auto body = responseJson (response);
        EXPECT_EQ (body["error"]["code"], "transition_rejected");
        EXPECT_EQ (body["requested_mode"], rejection.requested);
        EXPECT_EQ (body["mode"], rejection.actualText);
        EXPECT_EQ (status.transitionCalls, 1);
    }
}

TEST (StatusApiHandlerTests, MapsStatusOperationFailuresToStableHttpErrors)
{
    struct FailureCase
    {
        StatusTransitionFailure failure;
        int httpStatus;
        const char* errorCode;
    };
    const std::vector<FailureCase> cases {
        { StatusTransitionFailure::operationUnavailable, 503, "operation_unavailable" },
        { StatusTransitionFailure::operationTimedOut, 504, "operation_timeout" },
        { StatusTransitionFailure::operationFailed, 500, "operation_failed" }
    };

    for (const auto& failure : cases)
    {
        FakeStatus status;
        status.operationFailure = failure.failure;

        const auto response = put (status, R"({"mode":"ACQUIRE"})");

        expectJson (response, failure.httpStatus);
        EXPECT_EQ (responseJson (response)["error"]["code"], failure.errorCode);
        EXPECT_EQ (status.mode, StatusMode::Idle);
    }
}

TEST (StatusApiHandlerTests, ReturnsStableConflictWhenRecordNodesAreUnsynchronized)
{
    FakeStatus status;
    status.mode = StatusMode::Acquire;
    status.recordNodesUnsynchronized = true;

    const auto response = put (status, R"({"mode":"RECORD"})");

    expectJson (response, 409);
    const auto body = responseJson (response);
    EXPECT_EQ (body["error"]["code"], "record_nodes_not_synchronized");
    EXPECT_EQ (body["requested_mode"], "RECORD");
    EXPECT_EQ (body["mode"], "ACQUIRE");
    EXPECT_EQ (status.transitionCalls, 1);
    EXPECT_EQ (status.mode, StatusMode::Acquire);
}
