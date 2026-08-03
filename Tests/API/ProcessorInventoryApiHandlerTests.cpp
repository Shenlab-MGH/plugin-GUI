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
    bool insideDispatchedOperation = false;
    int readCount = 0;

    const json inventory {
        { "processors", json::array ({ {
              { "id", 22 }, { "name", "Bandpass Filter" },
              { "parameters", json::array ({ {
                    { "name", "High cutoff" }, { "type", "float" }, { "value", "6000" }
                } }) },
              { "streams", json::array ({ {
                    { "name", "Neuropixels AP" }, { "source_id", 11 },
                    { "sample_rate", 30000.0 }, { "channel_count", 384 },
                    { "parameters", json::array() }
                } }) },
              { "predecessor", 11 }
          } }) }
    };

    handleProcessorInventoryGet (
        request,
        response,
        [&] (std::function<void()> operation)
        {
            dispatched = true;
            insideDispatchedOperation = true;
            operation();
            insideDispatchedOperation = false;
            return true;
        },
        [&]
        {
            EXPECT_TRUE (insideDispatchedOperation);
            ++readCount;
            return inventory;
        },
        50ms);

    EXPECT_TRUE (dispatched);
    EXPECT_EQ (readCount, 1);
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

TEST (ProcessorInventoryApiHandlerTests, NeverReadsAfterDispatchFailureOrTimeout)
{
    int readCount = 0;
    const auto readInventory = [&]
    {
        ++readCount;
        return json ({ { "processors", json::array() } });
    };

    httplib::Request request;
    httplib::Response unavailable;
    handleProcessorInventoryGet (request, unavailable,
                                 [] (std::function<void()>) { return false; },
                                 readInventory, 50ms);
    EXPECT_EQ (unavailable.status, 503);
    EXPECT_EQ (readCount, 0);

    std::function<void()> pending;
    httplib::Response timeout;
    handleProcessorInventoryGet (request, timeout,
                                 [&] (std::function<void()> operation)
                                 {
                                     pending = std::move (operation);
                                     return true;
                                 },
                                 readInventory, 1ms);
    EXPECT_EQ (timeout.status, 504);
    EXPECT_EQ (readCount, 0);

    ASSERT_TRUE (pending);
    pending();
    EXPECT_EQ (readCount, 0);
}
