/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2024 Open Ephys

    ------------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    ------------------------------------------------------------------
*/

#include "../../Source/Utils/StatusHttpAdapter.h"
#include "../../Source/Utils/json.hpp"
#include "gtest/gtest.h"

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
using json = nlohmann::json;
using Mode = AcquisitionRecordingMode;
using Snapshot = AcquisitionRecordingControlSnapshot;

Snapshot snapshot (Mode mode)
{
    const auto acquiring = mode != Mode::idle;
    const auto recording = mode == Mode::record;

    Snapshot result;
    result.status = deriveAcquisitionRecordingStatus (
        acquiring,
        { { recording, recording } });
    result.recordNodes = {
        { 41, recording, recording, true, false }
    };
    result.audioDeviceAvailable = true;
    result.audioSampleRate = 30000.0;
    result.processorGraphReady = true;
    return result;
}

StatusControlResult getSuccess (Mode mode)
{
    StatusControlResult result;
    result.httpStatus = 200;
    result.achieved = snapshot (mode);
    return result;
}

StatusControlResult putSuccess (Mode mode)
{
    auto result = getSuccess (mode);
    result.requestedMode = mode;
    result.changed = true;
    result.unsynchronizedConfirmed = mode == Mode::record;
    return result;
}

StatusControlResult errorResult (
    int status,
    StringRef code,
    StringRef message)
{
    StatusControlResult result;
    result.httpStatus = status;
    result.errorCode = code;
    result.errorMessage = message;
    return result;
}

class LoopbackStatusServer
{
    enum class ListenerStartState
    {
        pending,
        committed,
        cancelled,
        finished
    };

public:
    explicit LoopbackStatusServer (StatusHttpHandlers handlers)
    {
        registerStatusHttpRoutes (server, std::move (handlers));
        port = server.bind_to_any_port ("127.0.0.1");
        if (port <= 0)
            throw std::runtime_error (
                "Could not bind the status test server to loopback.");

        std::promise<void> listenerThreadStarted;
        auto listenerThreadStartedFuture =
            listenerThreadStarted.get_future();
        serverThread = std::thread (
            [this,
             started = std::move (listenerThreadStarted)]() mutable
            {
                // Signal that cancellation can now synchronize with the
                // transition into the blocking listen call.
                started.set_value();
                auto expected = ListenerStartState::pending;
                if (! listenerStartState.compare_exchange_strong (
                        expected,
                        ListenerStartState::committed))
                    return;

                server.listen_after_bind();
                listenerStartState.store (
                    ListenerStartState::finished);
            });

        const auto deadline = std::chrono::steady_clock::now()
                              + std::chrono::seconds (2);
        if (listenerThreadStartedFuture.wait_until (deadline)
            != std::future_status::ready)
        {
            stopAndJoinListener();
            throw std::runtime_error (
                "The status test listener thread did not start in time.");
        }

        while (! server.is_running()
               && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();

        if (! server.is_running())
        {
            stopAndJoinListener();
            throw std::runtime_error (
                "The status test server did not start in time.");
        }
    }

    ~LoopbackStatusServer()
    {
        stopAndJoinListener();
    }

    httplib::Client client() const
    {
        return httplib::Client ("127.0.0.1", port);
    }

private:
    void stopAndJoinListener()
    {
        auto expected = ListenerStartState::pending;
        if (! listenerStartState.compare_exchange_strong (
                expected,
                ListenerStartState::cancelled))
        {
            // A committed listener must either publish running or return
            // before stop/join can be guaranteed not to miss the socket.
            while (! server.is_running()
                   && listenerStartState.load()
                          == ListenerStartState::committed)
                std::this_thread::yield();
        }

        server.stop();
        if (serverThread.joinable())
            serverThread.join();
    }

    httplib::Server server;
    int port = -1;
    std::atomic<ListenerStartState> listenerStartState {
        ListenerStartState::pending
    };
    std::thread serverThread;
};

json parseBody (const httplib::Result& response)
{
    return json::parse (response->body);
}

const json capabilities = json::array ({
    "oe.control.acquisition",
    "oe.control.recording"
});

void expectInvalidStatusRequestBody (const std::string& requestBody)
{
    int callCount = 0;
    StatusHttpHandlers handlers;
    handlers.put = [&] (StringRef)
    {
        ++callCount;
        return putSuccess (Mode::record);
    };
    LoopbackStatusServer server (std::move (handlers));

    auto client = server.client();
    const auto response = client.Put (
        "/api/status",
        requestBody,
        "application/json");

    ASSERT_TRUE (response);
    EXPECT_EQ (response->status, 400);
    EXPECT_EQ (
        response->get_header_value ("Content-Type"),
        "application/json");
    const auto document = parseBody (response);
    EXPECT_EQ (document["ok"], false);
    EXPECT_EQ (document["capabilities"], capabilities);
    ASSERT_TRUE (document.contains ("error"));
    EXPECT_EQ (document["error"]["code"], "invalid_request");
    ASSERT_TRUE (document["error"]["message"].is_string());
    EXPECT_FALSE (
        document["error"]["message"].get<std::string>().empty());
    EXPECT_EQ (document.size(), 3);
    EXPECT_EQ (callCount, 0);
}

} // namespace

TEST (StatusHttpAdapterTests,
      GetReturnsTheExactStatusJsonOverLoopback)
{
    int callCount = 0;
    StatusHttpHandlers handlers;
    handlers.get = [&]
    {
        ++callCount;
        return getSuccess (Mode::acquire);
    };
    LoopbackStatusServer server (std::move (handlers));

    auto client = server.client();
    const auto response = client.Get ("/api/status");

    ASSERT_TRUE (response);
    EXPECT_EQ (response->status, 200);
    EXPECT_EQ (
        response->get_header_value ("Content-Type"),
        "application/json");
    EXPECT_EQ (
        parseBody (response),
        json ({
            { "ok", true },
            { "capabilities", capabilities },
            { "mode", "ACQUIRE" },
            { "acquisition_active", true },
            { "recording_active", false },
            { "record_node_count", 1 },
            { "active_record_node_count", 0 },
            { "writer_thread_running_count", 0 },
            { "recording_consistent", true },
            { "record_nodes", json::array ({
                  { { "generation", 41 },
                    { "recording_active", false },
                    { "writer_thread_running", false },
                    { "recording_path_valid", true },
                    { "synchronized", false } }
              }) },
            { "audio_device_available", true },
            { "audio_sample_rate", 30000.0 },
            { "processor_graph_ready", true }
        }));
    EXPECT_EQ (callCount, 1);
}

TEST (StatusHttpAdapterTests,
      PutForwardsTheExactUtf8BodyOnceAndReturnsExactStatusJson)
{
    const std::string requestBody =
        u8" \n{\"mode\":\"RECORD\",\"subject\":\"\u5c0f\u9f20\U0001f401\"}\t";
    String receivedBody;
    int callCount = 0;
    StatusHttpHandlers handlers;
    handlers.put = [&] (StringRef body)
    {
        ++callCount;
        receivedBody = String (body);
        return putSuccess (Mode::record);
    };
    LoopbackStatusServer server (std::move (handlers));

    auto client = server.client();
    const auto response = client.Put (
        "/api/status",
        requestBody,
        "application/json");

    ASSERT_TRUE (response);
    EXPECT_EQ (response->status, 200);
    EXPECT_EQ (
        response->get_header_value ("Content-Type"),
        "application/json");
    EXPECT_EQ (receivedBody.toStdString(), requestBody);
    EXPECT_EQ (callCount, 1);
    EXPECT_EQ (
        parseBody (response),
        json ({
            { "ok", true },
            { "capabilities", capabilities },
            { "mode", "RECORD" },
            { "acquisition_active", true },
            { "recording_active", true },
            { "record_node_count", 1 },
            { "active_record_node_count", 1 },
            { "writer_thread_running_count", 1 },
            { "recording_consistent", true },
            { "record_nodes", json::array ({
                  { { "generation", 41 },
                    { "recording_active", true },
                    { "writer_thread_running", true },
                    { "recording_path_valid", true },
                    { "synchronized", false } }
              }) },
            { "audio_device_available", true },
            { "audio_sample_rate", 30000.0 },
            { "processor_graph_ready", true },
            { "requested_mode", "RECORD" },
            { "changed", true },
            { "unsynchronized_confirmed", true }
        }));
}

TEST (StatusHttpAdapterTests,
      PutRejectsARawEmbeddedNullBeforeInvokingTheHandler)
{
    std::string requestBody = R"({"mode":"AC)";
    requestBody.push_back ('\0');
    requestBody += R"(QUIRE"})";

    expectInvalidStatusRequestBody (requestBody);
}

TEST (StatusHttpAdapterTests,
      PutRejectsMalformedUtf8BeforeInvokingTheHandler)
{
    const std::vector<std::string> malformedSequences {
        std::string { static_cast<char> (0x80) },
        std::string { static_cast<char> (0xc0),
                      static_cast<char> (0xaf) },
        std::string { static_cast<char> (0xc3), '(' },
        std::string { static_cast<char> (0xe0),
                      static_cast<char> (0x80),
                      static_cast<char> (0x80) },
        std::string { static_cast<char> (0xed),
                      static_cast<char> (0xa0),
                      static_cast<char> (0x80) },
        std::string { static_cast<char> (0xf0),
                      static_cast<char> (0x80),
                      static_cast<char> (0x80),
                      static_cast<char> (0x80) },
        std::string { static_cast<char> (0xf4),
                      static_cast<char> (0x90),
                      static_cast<char> (0x80),
                      static_cast<char> (0x80) },
        std::string { static_cast<char> (0xe2),
                      static_cast<char> (0x82) }
    };

    for (const auto& malformed : malformedSequences)
    {
        SCOPED_TRACE (
            "malformed UTF-8 length "
            + std::to_string (malformed.size()));
        const auto requestBody =
            std::string { R"({"mode":")" }
            + malformed
            + R"("})";
        expectInvalidStatusRequestBody (requestBody);
    }
}

TEST (StatusHttpAdapterTests,
      PreservesEverySupportedHandlerHttpErrorWithoutFabricatingState)
{
    struct Case
    {
        int status;
        const char* code;
    };

    for (const auto& testCase : {
             Case { 400, "invalid_request" },
             Case { 409, "state_conflict" },
             Case { 500, "operation_failed" },
             Case { 503, "operation_unavailable" },
             Case { 504, "operation_timeout" } })
    {
        int callCount = 0;
        StatusHttpHandlers handlers;
        handlers.get = [&]
        {
            ++callCount;
            return errorResult (
                testCase.status,
                testCase.code,
                "Handler supplied error.");
        };
        LoopbackStatusServer server (std::move (handlers));

        auto client = server.client();
        const auto response = client.Get ("/api/status");

        ASSERT_TRUE (response);
        EXPECT_EQ (response->status, testCase.status);
        EXPECT_EQ (
            response->get_header_value ("Content-Type"),
            "application/json");
        EXPECT_EQ (
            parseBody (response),
            json ({
                { "ok", false },
                { "capabilities", capabilities },
                { "error", {
                    { "code", testCase.code },
                    { "message", "Handler supplied error." }
                } }
            }));
        EXPECT_EQ (callCount, 1);
    }
}

TEST (StatusHttpAdapterTests,
      MissingGetAndPutHandlersReturnStructuredUnavailableErrors)
{
    LoopbackStatusServer server (StatusHttpHandlers {});
    auto client = server.client();

    const auto getResponse = client.Get ("/api/status");
    const auto putResponse = client.Put (
        "/api/status",
        "{}",
        "application/json");

    for (const auto* response : { &getResponse, &putResponse })
    {
        ASSERT_TRUE (*response);
        EXPECT_EQ ((*response)->status, 503);
        EXPECT_EQ (
            (*response)->get_header_value ("Content-Type"),
            "application/json");
        EXPECT_EQ (
            parseBody (*response),
            json ({
                { "ok", false },
                { "capabilities", capabilities },
                { "error", {
                    { "code", "operation_unavailable" },
                    { "message", "The status operation is unavailable." }
                } }
            }));
    }
}

TEST (StatusHttpAdapterTests,
      StandardAndUnknownHandlerExceptionsReturnStructuredFailures)
{
    StatusHttpHandlers handlers;
    handlers.get = []() -> StatusControlResult
    {
        throw std::runtime_error ("get exploded");
    };
    handlers.put = [] (StringRef) -> StatusControlResult
    {
        throw 7;
    };
    LoopbackStatusServer server (std::move (handlers));
    auto client = server.client();

    const auto getResponse = client.Get ("/api/status");
    const auto putResponse = client.Put (
        "/api/status",
        "{}",
        "application/json");

    ASSERT_TRUE (getResponse);
    EXPECT_EQ (getResponse->status, 500);
    EXPECT_EQ (
        parseBody (getResponse),
        json ({
            { "ok", false },
            { "capabilities", capabilities },
            { "error", {
                { "code", "operation_failed" },
                { "message", "The status operation failed." }
            } }
        }));

    ASSERT_TRUE (putResponse);
    EXPECT_EQ (putResponse->status, 500);
    EXPECT_EQ (
        parseBody (putResponse),
        json ({
            { "ok", false },
            { "capabilities", capabilities },
            { "error", {
                { "code", "operation_failed" },
                { "message", "The status operation failed." }
            } }
        }));
}

TEST (StatusHttpAdapterTests,
      PostAndNonExactPathsRemainUnregistered)
{
    int getCount = 0;
    int putCount = 0;
    StatusHttpHandlers handlers;
    handlers.get = [&]
    {
        ++getCount;
        return getSuccess (Mode::idle);
    };
    handlers.put = [&] (StringRef)
    {
        ++putCount;
        return putSuccess (Mode::idle);
    };
    LoopbackStatusServer server (std::move (handlers));
    auto client = server.client();

    const auto postResponse = client.Post (
        "/api/status",
        "{}",
        "application/json");
    const auto suffixResponse = client.Get ("/api/status/extra");
    const auto trailingSlashResponse = client.Put (
        "/api/status/",
        "{}",
        "application/json");

    ASSERT_TRUE (postResponse);
    EXPECT_EQ (postResponse->status, 404);
    ASSERT_TRUE (suffixResponse);
    EXPECT_EQ (suffixResponse->status, 404);
    ASSERT_TRUE (trailingSlashResponse);
    EXPECT_EQ (trailingSlashResponse->status, 404);
    EXPECT_EQ (getCount, 0);
    EXPECT_EQ (putCount, 0);
}
