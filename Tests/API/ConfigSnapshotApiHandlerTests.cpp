#include "../../Source/Utils/ConfigSnapshotApiHandler.h"
#include "gtest/gtest.h"

#include <chrono>
#include <functional>
#include <stdexcept>

using json = nlohmann::json;
using namespace std::chrono_literals;

TEST (ConfigSnapshotApiHandlerTests, DispatchesReadAndPreservesExactOfficialInfoSchema)
{
    httplib::Request request;
    httplib::Response response;
    bool dispatched = false;
    bool read = false;

    handleConfigSnapshotGet (
        request,
        response,
        [&] (std::function<void()> operation)
        {
            dispatched = true;
            operation();
            return true;
        },
        [&]
        {
            read = true;
            return String ("<SETTINGS><SIGNALCHAIN /></SETTINGS>");
        },
        50ms);

    EXPECT_TRUE (dispatched);
    EXPECT_TRUE (read);
    EXPECT_EQ (response.status, 200);
    EXPECT_EQ (response.get_header_value ("Content-Type"), "application/json");
    EXPECT_EQ (json::parse (response.body),
               json ({ { "info", "<SETTINGS><SIGNALCHAIN /></SETTINGS>" } }));
}

TEST (ConfigSnapshotApiHandlerTests, MapsDispatchTimeoutAndReadFailureToStableErrors)
{
    struct Case
    {
        int expectedStatus;
        const char* expectedCode;
        std::function<bool (std::function<void()>)> dispatcher;
        std::function<String()> readSnapshot;
    };

    std::function<void()> pending;
    const Case cases[] {
        { 503, "operation_unavailable", [] (std::function<void()>) { return false; }, [] { return String(); } },
        { 504, "operation_timeout", [&] (std::function<void()> operation) { pending = std::move (operation); return true; }, [] { return String(); } },
        { 500, "operation_failed", [] (std::function<void()> operation) { operation(); return true; }, []() -> String { throw std::runtime_error ("snapshot failed"); } },
    };

    for (const auto& testCase : cases)
    {
        httplib::Request request;
        httplib::Response response;
        handleConfigSnapshotGet (request, response, testCase.dispatcher,
                                 testCase.readSnapshot, 1ms);

        EXPECT_EQ (response.status, testCase.expectedStatus);
        const auto body = json::parse (response.body);
        EXPECT_EQ (body["ok"], false);
        EXPECT_EQ (body["capability"], "oe.control.signal_chain.configuration");
        EXPECT_EQ (body["error"]["code"], testCase.expectedCode);
    }

    ASSERT_TRUE (pending);
    pending();
}
