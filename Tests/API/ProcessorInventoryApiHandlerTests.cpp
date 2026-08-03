#include "../../Source/Utils/ProcessorInventoryApiHandler.h"
#include "gtest/gtest.h"

#include <chrono>
#include <functional>
#include <stdexcept>

using json = nlohmann::json;
using namespace std::chrono_literals;

TEST (ProcessorInventoryApiHandlerTests, DispatchesReadAndPreservesTheExistingOfficialSchema)
{
    httplib::Request request;
    httplib::Response response;
    bool dispatched = false;

    const json inventory {
        { "processors", json::array ({ {
              { "id", 22 }, { "name", "Bandpass Filter" },
              { "parameters", json::array() }, { "streams", json::array() },
              { "predecessor", 11 }
          } }) }
    };

    handleProcessorInventoryGet (
        request,
        response,
        [&] (std::function<void()> operation)
        {
            dispatched = true;
            operation();
            return true;
        },
        [&] { return inventory; },
        50ms);

    EXPECT_TRUE (dispatched);
    EXPECT_EQ (response.status, 200);
    EXPECT_EQ (response.get_header_value ("Content-Type"), "application/json");
    EXPECT_EQ (json::parse (response.body), inventory);
}

TEST (ProcessorInventoryApiHandlerTests, MapsDispatchTimeoutAndReadFailureToStableErrors)
{
    struct Case
    {
        int expectedStatus;
        const char* expectedCode;
        std::function<bool (std::function<void()>)> dispatcher;
        std::function<json()> readInventory;
    };

    std::function<void()> pending;
    const Case cases[] {
        { 503, "operation_unavailable", [] (std::function<void()>) { return false; }, [] { return json::object(); } },
        { 504, "operation_timeout", [&] (std::function<void()> operation) { pending = std::move (operation); return true; }, [] { return json::object(); } },
        { 500, "operation_failed", [] (std::function<void()> operation) { operation(); return true; }, []() -> json { throw std::runtime_error ("inventory failed"); } },
    };

    for (const auto& testCase : cases)
    {
        httplib::Request request;
        httplib::Response response;
        handleProcessorInventoryGet (request, response, testCase.dispatcher,
                                     testCase.readInventory, 1ms);

        EXPECT_EQ (response.status, testCase.expectedStatus);
        const auto body = json::parse (response.body);
        EXPECT_EQ (body["ok"], false);
        EXPECT_EQ (body["capability"], "oe.control.signal_chain.processors");
        EXPECT_EQ (body["error"]["code"], testCase.expectedCode);
    }

    ASSERT_TRUE (pending);
    pending();
}
